#include <catch2/catch_test_macros.hpp>

#include <vector>

#include "btb/exchange.hpp"
#include "btb/rng.hpp"

using namespace btb;

namespace {

constexpr Price kMid = 20000;

OrderRequest limit(Side s, Price p, Qty q) { return {0, s, OrderType::Limit, p, q}; }
OrderRequest market(Side s, Qty q) { return {0, s, OrderType::Market, 0, q}; }

std::vector<Event> drain(Exchange& ex) {
  std::vector<Event> out(ex.events().pending());
  out.resize(ex.events().drain(out.data(), static_cast<std::uint32_t>(out.size())));
  return out;
}

bool has_event(const std::vector<Event>& ev, EventType t) {
  for (const auto& e : ev)
    if (e.type == t) return true;
  return false;
}

// Player (0) plus two unconstrained bots (1, 2).
struct Market {
  ContractSpec spec;
  Exchange ex{spec, kMid, EventFilter{true, true}};
  ParticipantId player = ex.add_participant(true);
  ParticipantId bot = ex.add_participant(false);
  ParticipantId bot2 = ex.add_participant(false);
};

}  // namespace

TEST_CASE("accounting: realised, unrealised and fees are exact") {
  Market m;
  const Cents tv = m.spec.tick_value;

  m.ex.submit(m.bot, limit(Side::Sell, kMid, 2));
  REQUIRE(m.ex.submit(m.player, market(Side::Buy, 2)).filled == 2);
  const Account& a = m.ex.account(m.player);
  CHECK(a.position == 2);
  CHECK(a.fees == 2 * m.spec.fee_per_contract);

  // Quote the market 8 ticks higher: mark = mid of 20008/20010 = 20009.
  m.ex.submit(m.bot, limit(Side::Buy, kMid + 8, 5));
  m.ex.submit(m.bot2, limit(Side::Sell, kMid + 10, 5));
  CHECK(m.ex.mark() == kMid + 9);
  CHECK(a.unrealized(m.ex.mark(), tv) == 2 * 9 * tv);
  CHECK(a.realized(tv) == -2 * m.spec.fee_per_contract);

  // Sell one into the bid at +8: realise 8 ticks on one contract.
  m.ex.submit(m.player, market(Side::Sell, 1));
  CHECK(a.position == 1);
  CHECK(a.realized(tv) == 8 * tv - 3 * m.spec.fee_per_contract);
  CHECK(a.unrealized(m.ex.mark(), tv) == 9 * tv);
  CHECK(m.ex.total_pnl(m.player) == a.realized(tv) + a.unrealized(m.ex.mark(), tv));
  CHECK(m.ex.zero_sum_holds());
}

TEST_CASE("accounting: flipping through zero resets the cost basis") {
  Account a;
  a.apply_fill(3, 100, 0);
  a.apply_fill(-5, 110, 0);  // close 3 long at +10, open 2 short at 110
  CHECK(a.position == -2);
  CHECK(a.open_cost == -220);
  CHECK(a.realized(1) == 30);
  CHECK(a.unrealized(105, 1) == 10);
}

TEST_CASE("risk: max position, margin and price band rejects") {
  Market m;
  m.ex.submit(m.bot, limit(Side::Sell, kMid + 1, 100));
  m.ex.submit(m.bot, limit(Side::Buy, kMid - 1, 100));

  // $25,000 equity / $6,000 initial margin => at most 4 contracts.
  CHECK(m.ex.submit(m.player, limit(Side::Buy, kMid - 1, 4)).ok());
  auto r = m.ex.submit(m.player, limit(Side::Buy, kMid - 1, 1));
  CHECK(r.reject == RejectReason::InsufficientMargin);
  // Worst case counts resting orders, so a sell does not offset them: the
  // sell side alone is 1 contract and is fine.
  CHECK(m.ex.submit(m.player, limit(Side::Sell, kMid + 5, 1)).ok());

  CHECK(m.ex.submit(m.player, limit(Side::Sell, kMid + 500, 1)).reject == RejectReason::PriceBand);
  CHECK(m.ex.submit(m.player, limit(Side::Buy, kMid - 1, 11)).reject == RejectReason::MaxPosition);
  CHECK(has_event(drain(m.ex), EventType::Reject));
}

TEST_CASE("risk: reducing orders are allowed even when over-margined") {
  Market m;
  m.ex.submit(m.bot, limit(Side::Sell, kMid, 4));
  REQUIRE(m.ex.submit(m.player, market(Side::Buy, 4)).filled == 4);
  // Market drops 30 ticks: equity falls but stays above maintenance.
  m.ex.submit(m.bot, limit(Side::Buy, kMid - 30, 50));
  m.ex.submit(m.bot2, limit(Side::Sell, kMid - 28, 50));
  REQUIRE(m.ex.equity(m.player) < 4 * m.spec.initial_margin);
  CHECK_FALSE(m.ex.account(m.player).liquidating);
  CHECK(m.ex.submit(m.player, limit(Side::Buy, kMid - 29, 1)).reject == RejectReason::InsufficientMargin);
  CHECK(m.ex.submit(m.player, market(Side::Sell, 2)).filled == 2);
  CHECK(m.ex.account(m.player).position == 2);
}

TEST_CASE("risk: market orders are protected by the price band") {
  Market m;
  m.ex.submit(m.bot, limit(Side::Sell, kMid + 1, 1));
  m.ex.submit(m.bot, limit(Side::Sell, kMid + 1000, 5));  // far beyond the band
  auto r = m.ex.submit(m.player, market(Side::Buy, 3));
  CHECK(r.filled == 1);
  CHECK(m.ex.account(m.player).position == 1);
}

TEST_CASE("liquidation: breach of maintenance margin cancels orders and flattens") {
  Market m;
  m.ex.submit(m.bot, limit(Side::Sell, kMid, 4));
  REQUIRE(m.ex.submit(m.player, market(Side::Buy, 4)).filled == 4);
  REQUIRE(m.ex.submit(m.player, limit(Side::Sell, kMid + 50, 2)).ok());
  REQUIRE(m.ex.tracked_orders().size() == 1);
  drain(m.ex);

  // Maintenance for 4 lots = $20,000, so a loss over $5,000 (100 ticks) breaches.
  m.ex.submit(m.bot2, limit(Side::Buy, kMid - 110, 10));
  m.ex.submit(m.bot, limit(Side::Sell, kMid - 108, 10));

  const auto ev = drain(m.ex);
  CHECK(has_event(ev, EventType::MarginCall));
  CHECK(has_event(ev, EventType::Liquidation));
  const Account& a = m.ex.account(m.player);
  CHECK(a.position == 0);
  CHECK_FALSE(a.liquidating);
  CHECK(a.open_sell == 0);
  CHECK(m.ex.tracked_orders().empty());
  CHECK(m.ex.zero_sum_holds());
}

TEST_CASE("liquidation retries when the book cannot absorb the position") {
  Market m;
  m.ex.submit(m.bot, limit(Side::Sell, kMid, 4));
  m.ex.submit(m.player, market(Side::Buy, 4));
  // Crash with only 1 lot of bid depth.
  m.ex.submit(m.bot2, limit(Side::Buy, kMid - 150, 1));
  m.ex.submit(m.bot, limit(Side::Sell, kMid - 149, 1));
  CHECK(m.ex.account(m.player).liquidating);
  CHECK(m.ex.account(m.player).position == 3);
  CHECK(m.ex.submit(m.player, limit(Side::Buy, kMid - 150, 1)).reject == RejectReason::Liquidating);

  m.ex.submit(m.bot2, limit(Side::Buy, kMid - 152, 10));  // liquidity returns
  m.ex.check_margins();
  CHECK(m.ex.account(m.player).position == 0);
  CHECK_FALSE(m.ex.account(m.player).liquidating);
  CHECK(m.ex.zero_sum_holds());
}

TEST_CASE("flatten cancels working orders and closes the position") {
  Market m;
  m.ex.submit(m.bot, limit(Side::Sell, kMid, 3));
  m.ex.submit(m.bot2, limit(Side::Buy, kMid - 2, 10));
  m.ex.submit(m.player, market(Side::Buy, 3));
  m.ex.submit(m.player, limit(Side::Buy, kMid - 5, 1));
  auto r = m.ex.flatten(m.player);
  CHECK(r.filled == 3);
  CHECK(m.ex.account(m.player).position == 0);
  CHECK(m.ex.account(m.player).open_buy == 0);
  CHECK(m.ex.tracked_orders().empty());
}

TEST_CASE("settlement: VWAP of the final window, cash-settled, market closed") {
  Market m;
  m.ex.set_time(1'000'000);
  m.ex.submit(m.bot, limit(Side::Sell, kMid, 2));
  m.ex.submit(m.player, market(Side::Buy, 2));  // before the window: excluded

  m.ex.begin_settlement_window(2'000'000);
  m.ex.set_time(2'500'000);
  m.ex.submit(m.bot, limit(Side::Sell, kMid + 10, 1));
  m.ex.submit(m.bot2, market(Side::Buy, 1));  // 1 @ +10
  m.ex.submit(m.bot, limit(Side::Sell, kMid + 20, 3));
  m.ex.submit(m.bot2, market(Side::Buy, 3));  // 3 @ +20
  // VWAP = (10*1 + 20*3) / 4 = 17.5 -> rounds to 18 ticks above.
  CHECK(m.ex.settlement_price() == kMid + 18);

  m.ex.submit(m.bot, limit(Side::Sell, kMid + 40, 5));  // resting, cancelled at expiry
  drain(m.ex);
  const Price px = m.ex.settle();
  CHECK(px == kMid + 18);
  CHECK(m.ex.closed());
  for (ParticipantId p = 0; p < 3; ++p) CHECK(m.ex.account(p).position == 0);
  CHECK(m.ex.book().order_count() == 0);
  CHECK(m.ex.total_pnl(m.player) == 2 * 18 * m.spec.tick_value - 2 * m.spec.fee_per_contract);
  CHECK(m.ex.zero_sum_holds());

  const auto ev = drain(m.ex);
  int settlements = 0;
  for (const auto& e : ev) settlements += e.type == EventType::Settlement;
  CHECK(settlements == 3);
  CHECK(m.ex.submit(m.player, market(Side::Buy, 1)).reject == RejectReason::RoundClosed);
}

TEST_CASE("events: ack, fill (maker flag), trade and cancel carry the right fields") {
  Market m;
  auto maker = m.ex.submit(m.player, limit(Side::Buy, kMid, 2));
  m.ex.submit(m.bot, limit(Side::Sell, kMid, 1));
  m.ex.cancel(m.player, maker.id);
  const auto ev = drain(m.ex);

  bool saw_maker_fill = false, saw_trade = false, saw_cancel = false;
  for (const auto& e : ev) {
    if (e.type == EventType::Fill && e.participant == m.player) {
      saw_maker_fill = true;
      CHECK(e.flags == 1);
      CHECK(e.value == 1);  // position after fill
      CHECK(e.side == Side::Buy);
    }
    if (e.type == EventType::Trade) {
      saw_trade = true;
      CHECK(e.side == Side::Sell);  // aggressor
      CHECK(e.aux == m.player);     // maker participant
    }
    if (e.type == EventType::Cancel && e.order_id == maker.id) {
      saw_cancel = true;
      CHECK(e.qty == 1);
    }
  }
  CHECK(saw_maker_fill);
  CHECK(saw_trade);
  CHECK(saw_cancel);
  CHECK(ev.front().type == EventType::Ack);
}

TEST_CASE("cancel refuses other participants' orders") {
  Market m;
  auto r = m.ex.submit(m.bot, limit(Side::Sell, kMid, 1));
  CHECK_FALSE(m.ex.cancel(m.player, r.id));
  CHECK(m.ex.book().order_count() == 1);
}

// Property test: random order flow from bots and a risk-checked player, with
// liquidations happening along the way. The market must stay zero-sum after
// every single operation, and resting-quantity bookkeeping must match the book.
TEST_CASE("zero-sum invariant holds under random flow across many seeds") {
  int liquidations = 0;
  for (std::uint64_t seed = 1; seed <= 25; ++seed) {
    ContractSpec spec;
    Exchange ex(spec, kMid);
    for (int i = 0; i < 8; ++i) ex.add_participant(i == 0);
    Rng rng(seed);
    std::vector<OrderId> ids;
    Price center = kMid;

    for (int step = 0; step < 10000; ++step) {
      ex.set_time(step * 1000);
      if (rng.chance(1, 50)) center += rng.range(-6, 6);
      if (rng.chance(1, 1500)) center += rng.range(-150, 150);  // shocks force liquidations
      const auto who = static_cast<ParticipantId>(rng.below(8));
      const auto op = rng.below(20);
      const Side side = rng.chance(1, 2) ? Side::Buy : Side::Sell;
      if (op < 12) {
        const Price px = center + rng.range(-8, 8);
        auto r = ex.submit(who, {who, side, OrderType::Limit, px, static_cast<Qty>(rng.range(1, 4))});
        if (r.resting) ids.push_back(r.id);
      } else if (op < 15) {
        ex.submit(who, {who, side, OrderType::Market, 0, static_cast<Qty>(rng.range(1, 3))});
      } else if (op < 18 && !ids.empty()) {
        const auto i = rng.below(static_cast<std::uint32_t>(ids.size()));
        const Order* o = ex.book().find(ids[i]);
        if (o) ex.cancel(o->owner, ids[i]);
        ids[i] = ids.back();
        ids.pop_back();
      } else if (op == 18) {
        ex.flatten(who);
      } else {
        ex.cancel_all(who);
      }
      ex.check_margins();
      REQUIRE(ex.zero_sum_holds());
      for (const auto& e : drain(ex)) liquidations += e.type == EventType::Liquidation;
      if (step == 8000) ex.begin_settlement_window(step * 1000);
    }

    // Open quantity bookkeeping matches the book exactly.
    Qty resting = 0, tracked = 0;
    LevelView lv[8192];
    for (Side s : {Side::Buy, Side::Sell}) {
      const auto n = ex.book().depth(s, lv, 8192);
      for (std::uint32_t i = 0; i < n; ++i) resting += lv[i].qty;
    }
    for (ParticipantId p = 0; p < 8; ++p) tracked += ex.account(p).open_buy + ex.account(p).open_sell;
    REQUIRE(resting == tracked);

    ex.settle();
    REQUIRE(ex.zero_sum_holds());
    for (ParticipantId p = 0; p < 8; ++p) REQUIRE(ex.account(p).position == 0);
  }
  CHECK(liquidations > 0);  // the property test must exercise the liquidation path
}
