// The exchange: order book + accounts + risk + event stream.
//
// Every order from every participant goes through Exchange::submit. The book
// does the matching; the exchange listens to it to keep positions, P&L and
// resting quantities exact, runs pre-trade risk for risk-checked accounts,
// liquidates accounts that breach maintenance margin, and settles the
// contract at expiry.
#pragma once

#include <vector>

#include "btb/account.hpp"
#include "btb/contract.hpp"
#include "btb/event.hpp"
#include "btb/order_book.hpp"
#include "btb/risk.hpp"

namespace btb {

struct EventFilter {
  bool bot_order_events = false;  // Ack/Fill/Cancel/Reject for non-player accounts
  bool book_deltas = false;       // per-level BookDelta events
};

struct ExchangeStats {
  std::uint64_t orders = 0;   // accepted + rejected submissions
  std::uint64_t cancels = 0;  // explicit cancel requests
  std::uint64_t trades = 0;
  std::uint64_t contracts = 0;
  std::uint64_t rejects = 0;
};

class Exchange final : private MatchListener {
 public:
  Exchange(const ContractSpec& spec, Price initial_price, EventFilter filter = {},
           std::uint32_t max_orders = 1u << 15);

  Exchange(const Exchange&) = delete;
  Exchange& operator=(const Exchange&) = delete;

  ParticipantId add_participant(bool risk_checked);
  std::size_t participant_count() const { return accounts_.size(); }

  void set_time(SimTime t) { now_ = t; }
  SimTime now() const { return now_; }

  SubmitResult submit(ParticipantId who, OrderRequest req);
  bool cancel(ParticipantId who, OrderId id);
  std::uint32_t cancel_all(ParticipantId who, CancelReason reason = CancelReason::User);
  // Cancels all orders, then closes the position with a market order.
  SubmitResult flatten(ParticipantId who);

  // Runs the maintenance-margin check for every risk-checked account and
  // liquidates breaches. Called by the simulation every tick and after every
  // order, since bot trades move the mark too.
  void check_margins();

  // Trades at or after `start` contribute to the settlement VWAP.
  void begin_settlement_window(SimTime start);
  Price settlement_price() const;
  // Cancels everything, cash-settles all positions and closes the market.
  Price settle();
  bool closed() const { return closed_; }

  void publish_news(std::uint32_t headline_id, std::int32_t sentiment);

  // ------------------------------------------------------------ inspection
  const OrderBook& book() const { return book_; }
  const Account& account(ParticipantId p) const { return accounts_[p]; }
  const ContractSpec& spec() const { return spec_; }
  const RiskEngine& risk() const { return risk_; }
  EventRing& events() { return events_; }
  const EventRing& events() const { return events_; }
  const ExchangeStats& stats() const { return stats_; }

  Price mark() const;
  bool has_last_trade() const { return last_trade_ > 0; }
  Price last_trade() const { return last_trade_; }
  // Cumulative aggressor-signed volume (buys +, sells -), public tape data.
  std::int64_t signed_volume() const { return signed_volume_; }
  Cents equity(ParticipantId p) const;
  Cents total_pnl(ParticipantId p) const { return accounts_[p].total_pnl(mark(), spec_.tick_value); }

  // Zero-sum check: sum of every participant's P&L plus all fees collected is
  // exactly zero, and net open interest is zero.
  bool zero_sum_holds() const;
  Cents total_fees() const;

  // Resting orders of risk-checked participants (the player's working orders),
  // oldest first.
  const std::vector<OrderId>& tracked_orders() const { return tracked_; }

 private:
  // MatchListener
  void on_accept(OrderId id, const OrderRequest& req) override;
  void on_trade(const Order& maker, OrderId taker_id, ParticipantId taker, Side aggressor, Price price,
                Qty qty) override;
  void on_cancel(OrderId id, ParticipantId owner, Side side, Price price, Qty remaining,
                 CancelReason reason) override;
  void on_level(Side side, Price price, Qty total) override;

  bool wants_order_events(ParticipantId p) const { return accounts_[p].risk_checked || filter_.bot_order_events; }
  void reject(ParticipantId who, const OrderRequest& req, RejectReason r);
  void liquidate(ParticipantId p);
  void untrack(OrderId id);
  void emit(Event e) {
    e.time_us = now_;
    events_.push(e);
  }

  ContractSpec spec_;
  RiskEngine risk_;
  EventFilter filter_;
  OrderBook book_;
  EventRing events_;
  std::vector<Account> accounts_;
  std::vector<OrderId> tracked_;
  ExchangeStats stats_;

  SimTime now_ = 0;
  Price initial_price_;
  Price last_trade_ = 0;
  Price settlement_px_ = 0;
  std::int64_t signed_volume_ = 0;
  bool closed_ = false;
  bool in_liquidation_ = false;

  SimTime vwap_start_ = -1;
  std::int64_t vwap_num_ = 0;  // sum price*qty (ticks*contracts)
  std::int64_t vwap_den_ = 0;  // sum qty
};

}  // namespace btb
