#include "lostart/SlotPool.h"

#include <catch2/catch_test_macros.hpp>

using namespace LA;

TEST_CASE("Slot pool allocates, claims and frees", "[slots]")
{
	SlotPool pool(3, 2);
	CHECK(pool.AllocatePrimary() == 0);
	CHECK(pool.AllocatePrimary() == 1);
	CHECK(pool.ClaimPrimary(2));
	CHECK_FALSE(pool.ClaimPrimary(2));
	CHECK_FALSE(pool.AllocatePrimary());
	CHECK(pool.FreePrimaryCount() == 0);
	pool.FreePrimary(1);
	CHECK(pool.AllocatePrimary() == 1);

	CHECK(pool.AllocateSubs(3).empty());  // atomic: not enough free
	CHECK(pool.FreeSubCount() == 2);
	CHECK(pool.AllocateSubs(2).size() == 2);
	CHECK(pool.UsedSubCount() == 2);
	pool.Reset();
	CHECK(pool.FreePrimaryCount() == 3);
	CHECK(pool.FreeSubCount() == 2);
	CHECK_FALSE(pool.ClaimSub(9));
}

TEST_CASE("Default capacity is 500 + 500", "[slots]")
{
	SlotPool pool;
	CHECK(pool.PrimaryCapacity() == 500);
	CHECK(pool.SubCapacity() == 500);
}
