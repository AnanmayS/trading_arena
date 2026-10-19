#include <catch2/catch_test_macros.hpp>

#include <vector>

#include "btb/exchange.hpp"
#include "btb/market_sim.hpp"
#include "btb/rng.hpp"

using namespace btb;

namespace {

constexpr Price kMid = 20000;

OrderRequest limit(Side s, Price p, Qty q) { return {0, s, OrderType::Limit, p, q}; }
OrderRequest market(Side s, Qty q) { return {0, s, OrderType::Market, 0, q}; }
OrderRequest stop(Side s, Price trigger, Qty q) { return {0, s, OrderType::Stop, trigger, q}; }

std::vector<Event> drain(Exchange& ex) {
  std::vector<Event> out(ex.events().pending());
  out.resize(ex.events().drain(out.data(), static_cast<std::uint32_t>(out.size())));
  return out;
}

const Event* find_event(const std::vector<Event>& ev, EventType t, ParticipantId who) {
  for (const auto& e : ev)
    if (e.type == t && e.participant == who) return &e;
  return nullptr;
}

// Player (0) plus three unconstrained bots.
struct Market {
  ContractSpec spec;
  Exchange ex{spec, kMid, EventFilter{true, true}};
  ParticipantId player = ex.add_participant(true);
  ParticipantId bot = ex.add_participant(false);
  ParticipantId bot2 = ex.add_participant(false);
  ParticipantId bot3 = ex.add_participant(false);

  // One contract changes hands at `px` between two bots: moves the last trade.
  void print(Price px) {
    ex.submit(bot2, limit(Side::Buy, px, 1));
    ex.submit(bot3, limit(Side::Sell, px, 1));
  }
  // Player buys (q > 0) or sells (q < 0) |q| at kMid from a bot.
  void open(Qty q) {
    ex.submit(bot, limit(q > 0 ? Side::Sell : Side::Buy, kMid, q > 0 ? q : -q));
    REQUIRE(ex.submit(player, market(q > 0 ? Side::Buy : Side::Sell, q > 0 ? q : -q)).filled == (q > 0 ? q : -q));
  }
};

}  // namespace

TEST_CASE("stops: a sell stop triggers when the last trade reaches the trigger, not before") {
  Market m;
  m.open(2);
  drain(m.ex);

  const auto s = m.ex.submit(m.player, stop(Side::Sell, kMid - 5, 2));
  REQUIRE(s.ok());
  CHECK(s.filled == 0);
  CHECK(s.resting == 0);
  REQUIRE(m.ex.stops().size() == 1);
  CHECK(m.ex.book().find(s.id) == nullptr);  // never rests in the book
  CHECK(m.ex.account(m.player).open_sell == 0);
  auto ev = drain(m.ex);
  const Event* ack = find_event(ev, EventType::Ack, m.player);
  REQUIRE(ack);
  CHECK(ack->order_id == s.id);
  CHECK(ack->price == kMid - 5);
  CHECK(ack->reason == static_cast<std::uint8_t>(OrderType::Stop));

  m.ex.submit(m.bot2, limit(Side::Buy, kMid - 5, 5));  // liquidity at the trigger
  m.print(kMid - 4);                                    // one tick short
  CHECK(m.ex.stops().size() == 1);
  CHECK(m.ex.account(m.player).position == 2);
  drain(m.ex);

  m.print(kMid - 5);  // reaches it
  CHECK(m.ex.stops().empty());
  CHECK(m.ex.account(m.player).position == 0);
  ev = drain(m.ex);
  const Event* trig = find_event(ev, EventType::Trigger, m.player);
  REQUIRE(trig);
  CHECK(trig->order_id == s.id);
  CHECK(trig->side == Side::Sell);
  CHECK(trig->price == kMid - 5);
  CHECK(trig->value == kMid - 5);
  CHECK(trig->qty == 2);
  // The Trigger comes first, then the market order's Ack and fills.
  const auto trig_at = trig - ev.data();
  const Event* child = find_event(ev, EventType::Ack, m.player);
  REQUIRE(child);
  CHECK(child - ev.data() > trig_at);
  CHECK(child->order_id > s.id);
  CHECK(m.ex.zero_sum_holds());
}

TEST_CASE("stops: a buy stop triggers when the last trade rises to the trigger") {
  Market m;
  m.open(-3);
  const auto s = m.ex.submit(m.player, stop(Side::Buy, kMid + 6, 3));
  REQUIRE(s.ok());
  m.ex.submit(m.bot, limit(Side::Sell, kMid + 6, 10));
  m.print(kMid + 5);
  CHECK(m.ex.account(m.player).position == -3);
  m.print(kMid + 7);  // trades through the trigger
  CHECK(m.ex.stops().empty());
  CHECK(m.ex.account(m.player).position == 0);
  CHECK(m.ex.zero_sum_holds());
}

TEST_CASE("stops: a gap through the trigger fills at the market, not at the stop price") {
  Market m;
  m.open(2);  // the book is empty again
  REQUIRE(m.ex.submit(m.player, stop(Side::Sell, kMid - 5, 2)).ok());

  // The next print is 40 ticks lower (a news gap): nothing traded in between.
  m.ex.submit(m.bot2, limit(Side::Buy, kMid - 40, 10));
  m.ex.submit(m.bot3, market(Side::Sell, 1));
  const auto ev = drain(m.ex);
  const Event* trig = find_event(ev, EventType::Trigger, m.player);
  REQUIRE(trig);
  CHECK(trig->price == kMid - 5);
  CHECK(trig->value == kMid - 40);
  const Account& a = m.ex.account(m.player);
  CHECK(a.position == 0);
  CHECK(a.realized(m.spec.tick_value) == -2 * 40 * m.spec.tick_value - 4 * m.spec.fee_per_contract);
  CHECK(m.ex.zero_sum_holds());
}

TEST_CASE("stops: rejects for the wrong side of the market, the band, bad input and the limit") {
  Market m;
  m.open(1);  // last trade = kMid
  CHECK(m.ex.submit(m.player, stop(Side::Sell, kMid, 1)).reject == RejectReason::StopPrice);
  CHECK(m.ex.submit(m.player, stop(Side::Sell, kMid + 3, 1)).reject == RejectReason::StopPrice);
  CHECK(m.ex.submit(m.player, stop(Side::Buy, kMid, 1)).reject == RejectReason::StopPrice);
  CHECK(m.ex.submit(m.player, stop(Side::Buy, kMid - 3, 1)).reject == RejectReason::StopPrice);
  CHECK(m.ex.submit(m.player, stop(Side::Sell, kMid - m.spec.price_band - 1, 1)).reject == RejectReason::PriceBand);
  CHECK(m.ex.submit(m.player, stop(Side::Sell, kMid - 5, 0)).reject == RejectReason::InvalidQty);
  CHECK(m.ex.submit(m.player, stop(Side::Sell, 0, 1)).reject == RejectReason::InvalidPrice);
  CHECK(m.ex.stops().empty());
  const auto ev = drain(m.ex);
  const Event* rej = find_event(ev, EventType::Reject, m.player);
  REQUIRE(rej);
  CHECK(rej->reason == static_cast<std::uint8_t>(RejectReason::StopPrice));

  for (std::uint32_t i = 0; i < kMaxStopsPerParticipant; ++i)
    REQUIRE(m.ex.submit(m.player, stop(Side::Sell, kMid - 1 - static_cast<Price>(i), 1)).ok());
  CHECK(m.ex.submit(m.player, stop(Side::Sell, kMid - 30, 1)).reject == RejectReason::TooManyStops);
  CHECK(m.ex.submit(m.bot, stop(Side::Sell, kMid - 30, 1)).ok());  // the limit is per participant
}

TEST_CASE("stops: every cancel path removes working stops") {
  Market m;
  m.open(4);
  drain(m.ex);

  SECTION("cancel by id, owner only") {
    const auto s = m.ex.submit(m.player, stop(Side::Sell, kMid - 5, 4));
    CHECK_FALSE(m.ex.cancel(m.bot, s.id));
    CHECK(m.ex.cancel(m.player, s.id));
    CHECK_FALSE(m.ex.cancel(m.player, s.id));
    CHECK(m.ex.stops().empty());
    const auto ev = drain(m.ex);
    const Event* c = find_event(ev, EventType::Cancel, m.player);
    REQUIRE(c);
    CHECK(c->order_id == s.id);
    CHECK(c->reason == static_cast<std::uint8_t>(CancelReason::User));
    CHECK(c->flags == 2);
    CHECK(c->price == kMid - 5);
    CHECK(c->qty == 4);
    // A cancelled stop never fires.
    m.print(kMid - 10);
    CHECK(m.ex.account(m.player).position == 4);
  }

  SECTION("cancel-all counts orders and stops") {
    m.ex.submit(m.player, stop(Side::Sell, kMid - 5, 4));
    m.ex.submit(m.player, stop(Side::Buy, kMid + 5, 1));
    m.ex.submit(m.player, limit(Side::Sell, kMid + 20, 1));
    m.ex.submit(m.bot, stop(Side::Sell, kMid - 5, 1));
    CHECK(m.ex.cancel_all(m.player) == 3);
    REQUIRE(m.ex.stops().size() == 1);
    CHECK(m.ex.stops().front().owner == m.bot);
  }

  SECTION("flatten") {
    m.ex.submit(m.bot2, limit(Side::Buy, kMid - 60, 20));
    m.ex.submit(m.player, stop(Side::Sell, kMid - 5, 4));
    m.ex.flatten(m.player);
    CHECK(m.ex.stops().empty());
    CHECK(m.ex.account(m.player).position == 0);
  }

  SECTION("liquidation") {
    m.ex.submit(m.player, stop(Side::Sell, kMid - 150, 4));
    drain(m.ex);
    // Quote the market 110 ticks lower without trading: the mark breaches
    // maintenance margin before any print reaches the stop.
    m.ex.submit(m.bot2, limit(Side::Buy, kMid - 110, 10));
    m.ex.submit(m.bot, limit(Side::Sell, kMid - 108, 10));
    const auto ev = drain(m.ex);
    REQUIRE(find_event(ev, EventType::Liquidation, m.player));
    CHECK_FALSE(find_event(ev, EventType::Trigger, m.player));
    const Event* c = find_event(ev, EventType::Cancel, m.player);
    REQUIRE(c);
    CHECK(c->reason == static_cast<std::uint8_t>(CancelReason::Risk));
    CHECK(c->flags == 2);
    CHECK(m.ex.stops().empty());
    CHECK(m.ex.account(m.player).position == 0);
  }

  SECTION("expiry") {
    m.ex.submit(m.player, stop(Side::Sell, kMid - 5, 4));
    m.ex.settle();
    CHECK(m.ex.stops().empty());
    const auto ev = drain(m.ex);
    const Event* c = find_event(ev, EventType::Cancel, m.player);
    REQUIRE(c);
    CHECK(c->reason == static_cast<std::uint8_t>(CancelReason::Expiry));
    CHECK(m.ex.submit(m.player, stop(Side::Sell, kMid - 5, 1)).reject == RejectReason::RoundClosed);
  }
  CHECK(m.ex.zero_sum_holds());
}

TEST_CASE("stops: not counted in worst-case exposure; the triggered order is risk-checked") {
  Market m;
  m.open(4);  // $25,000 / $6,000 initial margin: the account is full
  CHECK(m.ex.submit(m.player, limit(Side::Buy, kMid - 1, 1)).reject == RejectReason::InsufficientMargin);
  // A buy stop that would add exposure is accepted: stops are contingent.
  const auto s = m.ex.submit(m.player, stop(Side::Buy, kMid + 5, 1));
  REQUIRE(s.ok());
  CHECK(RiskEngine::worst_exposure(m.ex.account(m.player)) == 4);
  // Bracket: take-profit limit and protective stop on the same position.
  CHECK(m.ex.submit(m.player, limit(Side::Sell, kMid + 30, 4)).ok());
  CHECK(m.ex.submit(m.player, stop(Side::Sell, kMid - 10, 4)).ok());
  drain(m.ex);

  // When the buy stop fires, its market order still fails the margin check.
  m.ex.submit(m.bot, limit(Side::Sell, kMid + 6, 5));
  m.print(kMid + 5);
  const auto ev = drain(m.ex);
  const Event* trig = find_event(ev, EventType::Trigger, m.player);
  const Event* rej = find_event(ev, EventType::Reject, m.player);
  REQUIRE(trig);
  REQUIRE(rej);
  CHECK(rej > trig);
  CHECK(rej->reason == static_cast<std::uint8_t>(RejectReason::InsufficientMargin));
  CHECK(m.ex.account(m.player).position == 4);
  CHECK(m.ex.stops().size() == 1);  // the protective stop is still working
}

TEST_CASE("stops: a cascade resolves completely, oldest stop first") {
  Market m;
  m.print(kMid);
  m.ex.submit(m.bot2, limit(Side::Buy, kMid - 2, 2));
  m.ex.submit(m.bot2, limit(Side::Buy, kMid - 6, 5));
  const auto first = m.ex.submit(m.bot, stop(Side::Sell, kMid - 2, 3));
  const auto second = m.ex.submit(m.player, stop(Side::Sell, kMid - 6, 1));
  REQUIRE(first.ok());
  REQUIRE(second.ok());
  drain(m.ex);

  // One lot at -2 fires the bot's stop; its sweep prints at -6 and fires the
  // player's stop inside the same submit.
  m.ex.submit(m.bot3, market(Side::Sell, 1));
  const auto ev = drain(m.ex);
  std::vector<OrderId> fired;
  for (const auto& e : ev)
    if (e.type == EventType::Trigger) fired.push_back(e.order_id);
  CHECK(fired == std::vector<OrderId>{first.id, second.id});
  CHECK(m.ex.stops().empty());
  CHECK(m.ex.account(m.bot).position == -3);
  CHECK(m.ex.account(m.player).position == -1);
  CHECK(m.ex.zero_sum_holds());
}

// Random flow with stops from every participant: the market stays zero-sum
// after every operation and no stop survives settlement.
TEST_CASE("stops: zero-sum invariant holds under random flow with stops") {
  int triggers = 0;
  for (std::uint64_t seed = 1; seed <= 15; ++seed) {
    ContractSpec spec;
    Exchange ex(spec, kMid, EventFilter{true, false});
    for (int i = 0; i < 6; ++i) ex.add_participant(i == 0);
    Rng rng(seed * 31);
    std::vector<OrderId> stop_ids;
    Price center = kMid;

    for (int step = 0; step < 6000; ++step) {
      ex.set_time(step * 1000);
      if (rng.chance(1, 50)) center += rng.range(-6, 6);
      if (rng.chance(1, 1200)) center += rng.range(-120, 120);
      const auto who = static_cast<ParticipantId>(rng.below(6));
      const auto op = rng.below(20);
      const Side side = rng.chance(1, 2) ? Side::Buy : Side::Sell;
      if (op < 11) {
        ex.submit(who, {who, side, OrderType::Limit, center + rng.range(-8, 8), static_cast<Qty>(rng.range(1, 4))});
      } else if (op < 14) {
        ex.submit(who, {who, side, OrderType::Market, 0, static_cast<Qty>(rng.range(1, 3))});
      } else if (op < 17) {
        const Price ref = ex.has_last_trade() ? ex.last_trade() : ex.mark();
        const Price trig = side == Side::Sell ? ref - rng.range(1, 10) : ref + rng.range(1, 10);
        const auto r = ex.submit(who, {who, side, OrderType::Stop, trig, static_cast<Qty>(rng.range(1, 3))});
        if (r.ok()) stop_ids.push_back(r.id);
      } else if (op == 17 && !stop_ids.empty()) {
        const auto i = rng.below(static_cast<std::uint32_t>(stop_ids.size()));
        for (const StopOrder& st : ex.stops())
          if (st.id == stop_ids[i]) {
            ex.cancel(st.owner, st.id);
            break;
          }
        stop_ids[i] = stop_ids.back();
        stop_ids.pop_back();
      } else if (op == 18) {
        ex.flatten(who);
      } else {
        ex.cancel_all(who);
      }
      ex.check_stops();
      ex.check_margins();
      REQUIRE(ex.zero_sum_holds());
      for (const StopOrder& st : ex.stops()) REQUIRE_FALSE((st.owner == 0 && ex.account(0).liquidating));
      for (const auto& e : drain(ex)) triggers += e.type == EventType::Trigger;
    }
    ex.settle();
    REQUIRE(ex.stops().empty());
    REQUIRE(ex.zero_sum_holds());
  }
  CHECK(triggers > 100);
}

TEST_CASE("stops: replaying an action log with stops reproduces the round exactly") {
  int triggers = 0;
  for (std::uint64_t seed : {11ull, 12ull, 13ull}) {
    GameConfig cfg = make_config(Difficulty::Normal, Scenario::None);
    cfg.round_seconds = 60;
    MarketSim live(seed, cfg);
    Rng rng(seed * 13);
    std::uint64_t hash_check = 0;
    while (!live.finished()) {
      if (rng.chance(1, 30)) {
        const Exchange& ex = live.exchange();
        const Qty pos = ex.account(kPlayer).position;
        const Price last = ex.last_trade();
        switch (rng.below(5)) {
          case 0:
            live.player_submit(rng.chance(1, 2) ? Side::Buy : Side::Sell, OrderType::Market, 0,
                               static_cast<Qty>(rng.range(1, 3)));
            break;
          case 1:
          case 2:
            if (pos > 0) live.player_submit(Side::Sell, OrderType::Stop, last - rng.range(1, 5), pos);
            if (pos < 0) live.player_submit(Side::Buy, OrderType::Stop, last + rng.range(1, 5), -pos);
            break;
          case 3:
            if (!ex.stops().empty()) live.player_cancel(ex.stops().front().id);
            break;
          default: live.player_cancel_all(); break;
        }
      }
      live.run_ticks(1);
      std::vector<Event> ev(live.exchange().events().pending());
      live.exchange().events().drain(ev.data(), static_cast<std::uint32_t>(ev.size()));
      for (const auto& e : ev) triggers += e.type == EventType::Trigger && e.participant == kPlayer;
      hash_check = live.exchange().events().hash();
    }
    CHECK(MarketSim::replay(seed, cfg, live.action_log()) == hash_check);
  }
  CHECK(triggers > 5);
}
