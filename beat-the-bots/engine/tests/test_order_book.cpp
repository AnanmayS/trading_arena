#include <catch2/catch_test_macros.hpp>

#include <map>
#include <vector>

#include "btb/order_book.hpp"
#include "btb/rng.hpp"

using namespace btb;

namespace {

struct TradeRec {
  OrderId maker;
  OrderId taker;
  ParticipantId maker_owner;
  ParticipantId taker_owner;
  Side aggressor;
  Price price;
  Qty qty;
};

struct CancelRec {
  OrderId id;
  Qty remaining;
  CancelReason reason;
};

struct Recorder : MatchListener {
  std::vector<OrderId> accepted;
  std::vector<TradeRec> trades;
  std::vector<CancelRec> cancels;
  std::map<std::pair<int, Price>, Qty> levels;  // last reported level totals

  void on_accept(OrderId id, const OrderRequest&) override { accepted.push_back(id); }
  void on_trade(const Order& m, OrderId tid, ParticipantId t, Side ag, Price p, Qty q) override {
    trades.push_back({m.id, tid, m.owner, t, ag, p, q});
  }
  void on_cancel(OrderId id, ParticipantId, Side, Price, Qty rem, CancelReason r) override {
    cancels.push_back({id, rem, r});
  }
  void on_level(Side s, Price p, Qty total) override { levels[{static_cast<int>(s), p}] = total; }
};

OrderRequest limit(ParticipantId who, Side s, Price p, Qty q) { return {who, s, OrderType::Limit, p, q}; }
OrderRequest market(ParticipantId who, Side s, Qty q) { return {who, s, OrderType::Market, 0, q}; }
OrderRequest ioc(ParticipantId who, Side s, Price p, Qty q) { return {who, s, OrderType::IOC, p, q}; }

struct Fixture {
  Recorder rec;
  OrderBook book{BookConfig{20000, 1024, 4096}, &rec};
};

}  // namespace

TEST_CASE("resting orders set the top of book") {
  Fixture f;
  REQUIRE_FALSE(f.book.has_bid());
  REQUIRE_FALSE(f.book.has_ask());

  auto b = f.book.submit(limit(1, Side::Buy, 19999, 5));
  auto a = f.book.submit(limit(2, Side::Sell, 20001, 3));
  REQUIRE(b.ok());
  REQUIRE(a.ok());
  CHECK(b.resting == 5);
  CHECK(f.book.best_bid() == 19999);
  CHECK(f.book.best_ask() == 20001);
  CHECK(f.book.qty_at(19999) == 5);
  CHECK(f.book.order_count() == 2);
  CHECK(f.rec.trades.empty());
}

TEST_CASE("price-time priority: better price first, then earlier order") {
  Fixture f;
  auto first = f.book.submit(limit(1, Side::Sell, 20002, 2));
  auto second = f.book.submit(limit(2, Side::Sell, 20002, 2));
  auto better = f.book.submit(limit(3, Side::Sell, 20001, 1));

  auto taker = f.book.submit(market(9, Side::Buy, 4));
  REQUIRE(taker.filled == 4);
  REQUIRE(f.rec.trades.size() == 3);
  CHECK(f.rec.trades[0].maker == better.id);
  CHECK(f.rec.trades[0].price == 20001);
  CHECK(f.rec.trades[1].maker == first.id);
  CHECK(f.rec.trades[1].qty == 2);
  CHECK(f.rec.trades[2].maker == second.id);
  CHECK(f.rec.trades[2].qty == 1);
  CHECK(f.book.qty_at(20002) == 1);
  CHECK(f.rec.trades[0].aggressor == Side::Buy);
}

TEST_CASE("partial fill leaves the remainder resting at the limit") {
  Fixture f;
  f.book.submit(limit(1, Side::Sell, 20000, 3));
  auto r = f.book.submit(limit(2, Side::Buy, 20000, 5));
  CHECK(r.filled == 3);
  CHECK(r.resting == 2);
  CHECK(f.book.best_bid() == 20000);
  CHECK_FALSE(f.book.has_ask());
  CHECK(f.book.qty_at(20000) == 2);
}

TEST_CASE("aggressive limit sweeps multiple levels up to its price only") {
  Fixture f;
  for (Price p = 20001; p <= 20005; ++p) f.book.submit(limit(1, Side::Sell, p, 1));
  auto r = f.book.submit(limit(2, Side::Buy, 20003, 10));
  CHECK(r.filled == 3);
  CHECK(r.resting == 7);
  CHECK(f.book.best_bid() == 20003);
  CHECK(f.book.best_ask() == 20004);
  REQUIRE(f.rec.trades.size() == 3);
  CHECK(f.rec.trades[2].price == 20003);
}

TEST_CASE("trades execute at the maker's price") {
  Fixture f;
  f.book.submit(limit(1, Side::Buy, 20010, 1));
  f.book.submit(limit(2, Side::Sell, 19990, 1));
  REQUIRE(f.rec.trades.size() == 1);
  CHECK(f.rec.trades[0].price == 20010);
  CHECK(f.rec.trades[0].aggressor == Side::Sell);
}

TEST_CASE("market order remainder is cancelled, never rests") {
  Fixture f;
  f.book.submit(limit(1, Side::Buy, 19998, 2));
  auto r = f.book.submit(market(2, Side::Sell, 5));
  CHECK(r.filled == 2);
  CHECK(r.resting == 0);
  REQUIRE(f.rec.cancels.size() == 1);
  CHECK(f.rec.cancels[0].id == r.id);
  CHECK(f.rec.cancels[0].remaining == 3);
  CHECK(f.rec.cancels[0].reason == CancelReason::Unfilled);
  CHECK_FALSE(f.book.has_bid());
  CHECK_FALSE(f.book.has_ask());
}

TEST_CASE("market order into an empty book fills nothing") {
  Fixture f;
  auto r = f.book.submit(market(2, Side::Buy, 5));
  CHECK(r.ok());
  CHECK(r.filled == 0);
  CHECK(f.rec.cancels.size() == 1);
}

TEST_CASE("IOC fills what it can at its limit and cancels the rest") {
  Fixture f;
  f.book.submit(limit(1, Side::Sell, 20001, 2));
  f.book.submit(limit(1, Side::Sell, 20003, 2));
  auto r = f.book.submit(ioc(2, Side::Buy, 20002, 5));
  CHECK(r.filled == 2);
  CHECK(r.resting == 0);
  CHECK(f.book.best_ask() == 20003);
  CHECK_FALSE(f.book.has_bid());
  REQUIRE(f.rec.cancels.size() == 1);
  CHECK(f.rec.cancels[0].remaining == 3);
}

TEST_CASE("cancel removes an order and updates the top of book") {
  Fixture f;
  auto a = f.book.submit(limit(1, Side::Buy, 20000, 1));
  auto b = f.book.submit(limit(1, Side::Buy, 19995, 1));
  REQUIRE(f.book.cancel(a.id));
  CHECK(f.book.best_bid() == 19995);
  CHECK_FALSE(f.book.cancel(a.id));  // already gone
  REQUIRE(f.book.cancel(b.id));
  CHECK_FALSE(f.book.has_bid());
  CHECK(f.book.order_count() == 0);
  CHECK(f.rec.levels[{0, 20000}] == 0);
}

TEST_CASE("cancelling from the middle of a FIFO keeps queue order") {
  Fixture f;
  auto a = f.book.submit(limit(1, Side::Sell, 20000, 1));
  auto b = f.book.submit(limit(2, Side::Sell, 20000, 1));
  auto c = f.book.submit(limit(3, Side::Sell, 20000, 1));
  REQUIRE(f.book.cancel(b.id));
  f.book.submit(market(9, Side::Buy, 2));
  REQUIRE(f.rec.trades.size() == 2);
  CHECK(f.rec.trades[0].maker == a.id);
  CHECK(f.rec.trades[1].maker == c.id);
}

TEST_CASE("cancel_all removes only that participant's orders") {
  Fixture f;
  f.book.submit(limit(1, Side::Buy, 19990, 1));
  f.book.submit(limit(1, Side::Sell, 20010, 1));
  f.book.submit(limit(2, Side::Buy, 19991, 1));
  f.book.submit(limit(1, Side::Buy, 19991, 1));
  CHECK(f.book.cancel_all(1) == 3);
  CHECK(f.book.order_count() == 1);
  CHECK(f.book.best_bid() == 19991);
  CHECK_FALSE(f.book.has_ask());
}

TEST_CASE("self-trade prevention cancels the resting order") {
  Fixture f;
  auto own = f.book.submit(limit(1, Side::Sell, 20000, 2));
  auto other = f.book.submit(limit(2, Side::Sell, 20001, 2));
  auto r = f.book.submit(limit(1, Side::Buy, 20001, 3));

  REQUIRE(f.rec.cancels.size() == 1);
  CHECK(f.rec.cancels[0].id == own.id);
  CHECK(f.rec.cancels[0].reason == CancelReason::SelfTrade);
  REQUIRE(f.rec.trades.size() == 1);
  CHECK(f.rec.trades[0].maker == other.id);
  CHECK(r.filled == 2);
  CHECK(r.resting == 1);
  CHECK(f.book.best_bid() == 20001);
}

TEST_CASE("rejects invalid orders without side effects") {
  Fixture f;
  CHECK(f.book.submit(limit(1, Side::Buy, 20000, 0)).reject == RejectReason::InvalidQty);
  CHECK(f.book.submit(limit(1, Side::Buy, 20000, -1)).reject == RejectReason::InvalidQty);
  CHECK(f.book.submit(limit(1, Side::Buy, 0, 1)).reject == RejectReason::InvalidPrice);
  CHECK(f.rec.accepted.empty());
  CHECK(f.book.order_count() == 0);
}

TEST_CASE("pool exhaustion rejects new resting orders") {
  Recorder rec;
  OrderBook book{BookConfig{20000, 1024, 4}, &rec};
  for (int i = 0; i < 4; ++i) REQUIRE(book.submit(limit(1, Side::Buy, 19990 + i, 1)).ok());
  CHECK(book.submit(limit(1, Side::Buy, 19980, 1)).reject == RejectReason::PoolExhausted);
  // Market orders need no slot and still work.
  CHECK(book.submit(market(2, Side::Sell, 1)).filled == 1);
  CHECK(book.submit(limit(1, Side::Buy, 19980, 1)).ok());
}

TEST_CASE("the price window recentres when prices drift, and rejects impossible spans") {
  Recorder rec;
  OrderBook book{BookConfig{20000, 128, 64}, &rec};  // window [19936, 20064)
  REQUIRE(book.submit(limit(1, Side::Buy, 20000, 1)).ok());
  REQUIRE(book.submit(limit(1, Side::Sell, 20050, 1)).ok());
  // Out of window but the span 20000..20100 fits in 128 ticks: recentre.
  REQUIRE(book.submit(limit(2, Side::Sell, 20100, 1)).ok());
  CHECK(book.best_bid() == 20000);
  CHECK(book.best_ask() == 20050);
  CHECK(book.qty_at(20100) == 1);
  // A span wider than the window cannot be represented.
  CHECK(book.submit(limit(2, Side::Sell, 20200, 1)).reject == RejectReason::PriceOutOfRange);
  // Matching still works after recentring.
  CHECK(book.submit(market(3, Side::Buy, 2)).filled == 2);
}

TEST_CASE("depth reports levels from the best price outward") {
  Fixture f;
  f.book.submit(limit(1, Side::Buy, 19990, 1));
  f.book.submit(limit(1, Side::Buy, 19995, 2));
  f.book.submit(limit(2, Side::Buy, 19995, 3));
  f.book.submit(limit(1, Side::Sell, 20005, 4));
  LevelView lv[8];
  REQUIRE(f.book.depth(Side::Buy, lv, 8) == 2);
  CHECK(lv[0].price == 19995);
  CHECK(lv[0].qty == 5);
  CHECK(lv[0].orders == 2);
  CHECK(lv[1].price == 19990);
  REQUIRE(f.book.depth(Side::Sell, lv, 8) == 1);
  CHECK(lv[0].qty == 4);
  CHECK(f.book.depth(Side::Buy, lv, 1) == 1);
}

// Randomised cross-check against a naive reference model: total quantity is
// conserved, the book is never crossed, and level totals match the orders.
TEST_CASE("randomised operations keep the book consistent") {
  Recorder rec;
  OrderBook book{BookConfig{20000, 512, 8192}, &rec};
  Rng rng(42);
  std::vector<OrderId> live;
  std::int64_t submitted = 0, traded = 0, cancelled = 0;

  for (int step = 0; step < 50000; ++step) {
    const auto op = rng.below(10);
    if (op < 6 || live.empty()) {
      const Side s = rng.chance(1, 2) ? Side::Buy : Side::Sell;
      const Qty q = static_cast<Qty>(rng.range(1, 5));
      const auto t = rng.below(10);
      OrderRequest req{static_cast<ParticipantId>(rng.below(8)), s,
                       t < 8 ? OrderType::Limit : (t == 8 ? OrderType::IOC : OrderType::Market),
                       20000 + rng.range(-20, 20), q};
      const size_t before_trades = rec.trades.size();
      const size_t before_cancels = rec.cancels.size();
      auto r = book.submit(req);
      REQUIRE(r.ok());
      submitted += q;
      for (size_t i = before_trades; i < rec.trades.size(); ++i) traded += 2 * rec.trades[i].qty;
      for (size_t i = before_cancels; i < rec.cancels.size(); ++i) cancelled += rec.cancels[i].remaining;
      if (r.resting > 0) live.push_back(r.id);
    } else {
      const auto i = rng.below(static_cast<std::uint32_t>(live.size()));
      const size_t before = rec.cancels.size();
      book.cancel(live[i]);
      for (size_t k = before; k < rec.cancels.size(); ++k) cancelled += rec.cancels[k].remaining;
      live[i] = live.back();
      live.pop_back();
    }

    if (book.has_bid() && book.has_ask()) REQUIRE(book.best_bid() < book.best_ask());
  }

  // Conservation: every submitted contract is resting, traded (counted once
  // per side), or cancelled.
  std::int64_t resting = 0;
  LevelView lv[1024];
  for (Side s : {Side::Buy, Side::Sell}) {
    const auto n = book.depth(s, lv, 1024);
    for (std::uint32_t i = 0; i < n; ++i) resting += lv[i].qty;
  }
  CHECK(submitted == resting + traded + cancelled);
  CHECK(rec.trades.size() > 1000);
}
