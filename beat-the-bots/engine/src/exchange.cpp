#include "btb/exchange.hpp"

#include <algorithm>
#include <cstdlib>

namespace btb {

Exchange::Exchange(const ContractSpec& spec, Price initial_price, EventFilter filter, std::uint32_t max_orders)
    : spec_(spec),
      risk_(spec),
      filter_(filter),
      book_(BookConfig{initial_price, 8192, max_orders}),
      initial_price_(initial_price) {
  book_.set_listener(this);
}

ParticipantId Exchange::add_participant(bool risk_checked) {
  accounts_.push_back(Account{});
  accounts_.back().risk_checked = risk_checked;
  return static_cast<ParticipantId>(accounts_.size() - 1);
}

Price Exchange::mark() const {
  if (closed_) return settlement_px_;
  if (book_.has_bid() && book_.has_ask()) return (book_.best_bid() + book_.best_ask()) / 2;
  if (last_trade_ > 0) return last_trade_;
  return initial_price_;
}

Cents Exchange::equity(ParticipantId p) const { return spec_.starting_cash + total_pnl(p); }

SubmitResult Exchange::submit(ParticipantId who, OrderRequest req) {
  req.owner = who;
  ++stats_.orders;
  if (closed_) {
    reject(who, req, RejectReason::RoundClosed);
    return {kNoOrder, RejectReason::RoundClosed, 0, 0};
  }

  Account& a = accounts_[who];
  if (a.risk_checked) {
    const Price m = mark();
    if (const RejectReason r = risk_.pre_trade(a, req, m, equity(who)); r != RejectReason::None) {
      reject(who, req, r);
      return {kNoOrder, r, 0, 0};
    }
    // Market protection: a player market order becomes an IOC limited to the
    // price band, so a thin book can never fill it at an absurd price.
    if (req.type == OrderType::Market) {
      req.type = OrderType::IOC;
      req.price = req.side == Side::Buy ? m + spec_.price_band : std::max<Price>(1, m - spec_.price_band);
    }
  }

  const SubmitResult res = book_.submit(req);
  if (!res.ok()) {
    reject(who, req, res.reject);
    return res;
  }
  if (res.resting > 0) {
    (req.side == Side::Buy ? a.open_buy : a.open_sell) += res.resting;
    if (a.risk_checked) tracked_.push_back(res.id);
  }
  check_margins();
  return res;
}

bool Exchange::cancel(ParticipantId who, OrderId id) {
  ++stats_.cancels;
  const Order* o = book_.find(id);
  if (!o || o->owner != who) return false;
  return book_.cancel(id, CancelReason::User);
}

std::uint32_t Exchange::cancel_all(ParticipantId who, CancelReason reason) {
  ++stats_.cancels;
  return book_.cancel_all(who, reason);
}

SubmitResult Exchange::flatten(ParticipantId who) {
  cancel_all(who);
  const Qty pos = accounts_[who].position;
  if (pos == 0) return {};
  return submit(who, {who, pos > 0 ? Side::Sell : Side::Buy, OrderType::Market, 0, std::abs(pos)});
}

void Exchange::check_margins() {
  if (in_liquidation_ || closed_) return;
  for (ParticipantId p = 0; p < accounts_.size(); ++p) {
    const Account& a = accounts_[p];
    if (!a.risk_checked) continue;
    if (a.liquidating || risk_.breaches_maintenance(a, equity(p))) liquidate(p);
  }
}

// Margin call: pull every working order, then close the position at market
// with no price protection. If the book cannot absorb it all, the account
// stays in liquidation and the remainder is retried on the next check.
void Exchange::liquidate(ParticipantId p) {
  in_liquidation_ = true;
  Account& a = accounts_[p];
  if (!a.liquidating) {
    emit({.price = mark(), .value = equity(p), .qty = a.position, .participant = p, .type = EventType::MarginCall});
    a.liquidating = true;
    book_.cancel_all(p, CancelReason::Risk);
    emit({.qty = a.position, .participant = p, .type = EventType::Liquidation});
  }
  if (a.position != 0) {
    ++stats_.orders;
    book_.submit({p, a.position > 0 ? Side::Sell : Side::Buy, OrderType::Market, 0, std::abs(a.position)});
  }
  if (a.position == 0) a.liquidating = false;
  in_liquidation_ = false;
}

void Exchange::begin_settlement_window(SimTime start) { vwap_start_ = start; }

Price Exchange::settlement_price() const {
  if (vwap_den_ > 0) return (vwap_num_ + vwap_den_ / 2) / vwap_den_;
  return mark();
}

Price Exchange::settle() {
  if (closed_) return settlement_px_;
  const Price px = settlement_price();
  for (ParticipantId p = 0; p < accounts_.size(); ++p) book_.cancel_all(p, CancelReason::Expiry);
  settlement_px_ = px;
  closed_ = true;
  for (ParticipantId p = 0; p < accounts_.size(); ++p) {
    Account& a = accounts_[p];
    const Qty pos = a.position;
    a.settle(px);
    a.liquidating = false;
    emit({.price = px,
          .value = a.total_pnl(px, spec_.tick_value),
          .qty = pos,
          .participant = p,
          .type = EventType::Settlement});
  }
  return px;
}

void Exchange::publish_news(std::uint32_t headline_id, std::int32_t sentiment) {
  emit({.value = sentiment, .type = EventType::News, .aux = headline_id});
}

bool Exchange::zero_sum_holds() const {
  const Price m = mark();
  Cents pnl = 0;
  std::int64_t open_interest = 0;
  for (const Account& a : accounts_) {
    pnl += a.total_pnl(m, spec_.tick_value);
    open_interest += a.position;
  }
  return open_interest == 0 && pnl + total_fees() == 0;
}

Cents Exchange::total_fees() const {
  Cents f = 0;
  for (const Account& a : accounts_) f += a.fees;
  return f;
}

// ------------------------------------------------------------ book callbacks

void Exchange::on_accept(OrderId id, const OrderRequest& req) {
  if (!wants_order_events(req.owner)) return;
  emit({.price = req.price,
        .order_id = id,
        .qty = req.qty,
        .participant = req.owner,
        .type = EventType::Ack,
        .side = req.side,
        .reason = static_cast<std::uint8_t>(req.type)});
}

void Exchange::on_trade(const Order& maker, OrderId taker_id, ParticipantId taker, Side aggressor, Price price,
                        Qty qty) {
  Account& m = accounts_[maker.owner];
  Account& t = accounts_[taker];
  m.apply_fill(signed_qty(maker.side, qty), price, spec_.fee_per_contract);
  t.apply_fill(signed_qty(aggressor, qty), price, spec_.fee_per_contract);
  (maker.side == Side::Buy ? m.open_buy : m.open_sell) -= qty;
  if (maker.qty == 0 && m.risk_checked) untrack(maker.id);

  last_trade_ = price;
  ++stats_.trades;
  stats_.contracts += static_cast<std::uint64_t>(qty);
  if (vwap_start_ >= 0 && now_ >= vwap_start_) {
    vwap_num_ += price * qty;
    vwap_den_ += qty;
  }

  emit({.price = price,
        .value = static_cast<std::int64_t>(maker.id),
        .order_id = taker_id,
        .qty = qty,
        .participant = taker,
        .type = EventType::Trade,
        .side = aggressor,
        .aux = maker.owner});
  if (wants_order_events(maker.owner))
    emit({.price = price,
          .value = m.position,
          .order_id = maker.id,
          .qty = qty,
          .participant = maker.owner,
          .type = EventType::Fill,
          .side = maker.side,
          .flags = 1});
  if (wants_order_events(taker))
    emit({.price = price,
          .value = t.position,
          .order_id = taker_id,
          .qty = qty,
          .participant = taker,
          .type = EventType::Fill,
          .side = aggressor});
}

void Exchange::on_cancel(OrderId id, ParticipantId owner, Side side, Price price, Qty remaining,
                         CancelReason reason) {
  Account& a = accounts_[owner];
  // An Unfilled cancel is the remainder of an incoming IOC/market order that
  // never rested, so it was never counted as open quantity.
  if (reason != CancelReason::Unfilled) {
    (side == Side::Buy ? a.open_buy : a.open_sell) -= remaining;
    if (a.risk_checked) untrack(id);
  }
  if (!wants_order_events(owner)) return;
  emit({.price = price,
        .order_id = id,
        .qty = remaining,
        .participant = owner,
        .type = EventType::Cancel,
        .side = side,
        .reason = static_cast<std::uint8_t>(reason)});
}

void Exchange::on_level(Side side, Price price, Qty total) {
  if (!filter_.book_deltas) return;
  emit({.price = price, .qty = total, .type = EventType::BookDelta, .side = side});
}

void Exchange::reject(ParticipantId who, const OrderRequest& req, RejectReason r) {
  ++stats_.rejects;
  if (!wants_order_events(who)) return;
  emit({.price = req.price,
        .qty = req.qty,
        .participant = who,
        .type = EventType::Reject,
        .side = req.side,
        .reason = static_cast<std::uint8_t>(r)});
}

void Exchange::untrack(OrderId id) {
  const auto it = std::find(tracked_.begin(), tracked_.end(), id);
  if (it != tracked_.end()) tracked_.erase(it);
}

}  // namespace btb
