#include <catch2/catch.hpp>

#include <queue>

#include "libslic3r/MutablePriorityQueue.hpp"

// based on https://raw.githubusercontent.com/rollbear/prio_queue/master/self_test.cpp
// original source Copyright Björn Fahller 2015, Boost Software License, Version 1.0, http://www.boost.org/LICENSE_1_0.txt

TEST_CASE("Skip addressing", "[MutableSkipHeapPriorityQueue]") {
    using skip_addressing = SkipHeapAddressing<8>;
    SECTION("block root") {
        REQUIRE(skip_addressing::is_block_root(1));
        REQUIRE(skip_addressing::is_block_root(9));
        REQUIRE(skip_addressing::is_block_root(17));
        REQUIRE(skip_addressing::is_block_root(73));
        REQUIRE(! skip_addressing::is_block_root(2));
        REQUIRE(! skip_addressing::is_block_root(3));
        REQUIRE(! skip_addressing::is_block_root(4));
        REQUIRE(! skip_addressing::is_block_root(7));
        REQUIRE(! skip_addressing::is_block_root(31));
    }
    SECTION("block leaf") {
        REQUIRE(! skip_addressing::is_block_leaf(1));
        REQUIRE(! skip_addressing::is_block_leaf(2));
        REQUIRE(! skip_addressing::is_block_leaf(3));
        REQUIRE(skip_addressing::is_block_leaf(4));
        REQUIRE(skip_addressing::is_block_leaf(5));
        REQUIRE(skip_addressing::is_block_leaf(6));
        REQUIRE(skip_addressing::is_block_leaf(7));
        REQUIRE(skip_addressing::is_block_leaf(28));
        REQUIRE(skip_addressing::is_block_leaf(29));
        REQUIRE(skip_addressing::is_block_leaf(30));
        REQUIRE(! skip_addressing::is_block_leaf(257));
        REQUIRE(skip_addressing::is_block_leaf(255));
    }
    SECTION("Obtaining child") {
        REQUIRE(skip_addressing::child_of(1) == 2);
        REQUIRE(skip_addressing::child_of(2) == 4);
        REQUIRE(skip_addressing::child_of(3) == 6);
        REQUIRE(skip_addressing::child_of(4) == 9);
        REQUIRE(skip_addressing::child_of(31) == 249);
    }
    SECTION("Obtaining parent") {
        REQUIRE(skip_addressing::parent_of(2) == 1);
        REQUIRE(skip_addressing::parent_of(3) == 1);
        REQUIRE(skip_addressing::parent_of(6) == 3);
        REQUIRE(skip_addressing::parent_of(7) == 3);
        REQUIRE(skip_addressing::parent_of(9) == 4);
        REQUIRE(skip_addressing::parent_of(17) == 4);
        REQUIRE(skip_addressing::parent_of(33) == 5);
        REQUIRE(skip_addressing::parent_of(29) == 26);
        REQUIRE(skip_addressing::parent_of(1097) == 140);
    }
}

template<size_t block_size = 16>
static auto make_test_priority_queue()
{
    return make_miniheap_mutable_priority_queue<std::pair<int, size_t>, block_size, false>(
        [](std::pair<int, size_t> &v, size_t idx){ v.second = idx; },
        [](std::pair<int, size_t> &l, std::pair<int, size_t> &r){ return l.first < r.first; });
}

TEST_CASE("Mutable priority queue - basic tests", "[MutableSkipHeapPriorityQueue]") {
    SECTION("a default constructed queue is empty") {
        auto q = make_test_priority_queue();
        REQUIRE(q.empty());
        REQUIRE(q.size() == 0);
    }
    SECTION("an empty queue is not empty when one element is inserted") {
        auto q = make_test_priority_queue();
        q.push(std::make_pair(1, 0U));
        REQUIRE(!q.empty());
        REQUIRE(q.size() == 1);
    }
    SECTION("a queue with one element has it on top") {
        auto q = make_test_priority_queue();
        q.push(std::make_pair(8, 0U));
        REQUIRE(q.top().first == 8);
    }
    SECTION("a queue with one element becomes empty when popped") {
        auto q = make_test_priority_queue();
        q.push(std::make_pair(9, 0U));
        q.pop();
        REQUIRE(q.empty());
        REQUIRE(q.size() == 0);
    }
    SECTION("insert sorted stays sorted") {
        auto q = make_test_priority_queue();
        for (auto i : { 1, 2, 3, 4, 5, 6, 7, 8 })
            q.push(std::make_pair(i, 0U));
        REQUIRE(q.top().first == 1);
        q.pop();
        REQUIRE(q.top().first == 2);
        q.pop();
        REQUIRE(q.top().first == 3);
        q.pop();
        REQUIRE(q.top().first == 4);
        q.pop();
        REQUIRE(q.top().first == 5);
        q.pop();
        REQUIRE(q.top().first == 6);
        q.pop();
        REQUIRE(q.top().first == 7);
        q.pop();
        REQUIRE(q.top().first == 8);
        q.pop();
        REQUIRE(q.empty());
    }
    SECTION("randomly inserted elements are popped sorted") {
        auto q = make_test_priority_queue();
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dist(1, 100000);
        int n[36000];
        for (auto& i : n) {
            i = dist(gen);
            q.push(std::make_pair(i, 0U));
        }

        REQUIRE(!q.empty());
        REQUIRE(q.size() == 36000);
        std::sort(std::begin(n), std::end(n));
        for (auto i : n) {
            REQUIRE(q.top().first == i);
            q.pop();
        }
        REQUIRE(q.empty());
    }
}

TEST_CASE("Mutable priority queue - reshedule first", "[MutableSkipHeapPriorityQueue]") {
    SECTION("reschedule top with highest prio leaves order unchanged") {
        auto q = make_miniheap_mutable_priority_queue<std::pair<std::pair<int, int*>, size_t>, 4, false>(
            [](std::pair<std::pair<int, int*>, size_t>& v, size_t idx) { v.second = idx; },
            [](std::pair<std::pair<int, int*>, size_t>& l, std::pair<std::pair<int, int*>, size_t>& r) { return l.first.first < r.first.first; });

        //              0  1   2   3  4   5  6   7   8
        int nums[] = { 32, 1, 88, 16, 9, 11, 3, 22, 23 };
        for (auto &i : nums)
            q.push(std::make_pair(std::make_pair(i, &i), 0U));
        REQUIRE(q.top().first.first == 1);
        REQUIRE(q.top().first.second == &nums[1]);
        REQUIRE(*q.top().first.second == 1);

        // Update the top element.
        q.top().first.first = 2;
        q.update(1);

        REQUIRE(q.top().first.first == 2);
        REQUIRE(q.top().first.second == &nums[1]);
        q.pop();
        REQUIRE(q.top().first.first == 3);
        REQUIRE(q.top().first.second == &nums[6]);
        q.pop();
        REQUIRE(q.top().first.first == 9);
        REQUIRE(q.top().first.second == &nums[4]);
        q.pop();
        REQUIRE(q.top().first.first == 11);
        REQUIRE(q.top().first.second == &nums[5]);
        q.pop();
        REQUIRE(q.top().first.first == 16);
        REQUIRE(q.top().first.second == &nums[3]);
        q.pop();
        REQUIRE(q.top().first.first == 22);
        REQUIRE(q.top().first.second == &nums[7]);
        q.pop();
        REQUIRE(q.top().first.first == 23);
        REQUIRE(q.top().first.second == &nums[8]);
        q.pop();
        REQUIRE(q.top().first.first == 32);
        REQUIRE(q.top().first.second == &nums[0]);
        q.pop();
        REQUIRE(q.top().first.first == 88);
        REQUIRE(q.top().first.second == &nums[2]);
        q.pop();
        REQUIRE(q.empty());
    }
    SECTION("reschedule to mid range moves element to correct place") {
        auto q = make_miniheap_mutable_priority_queue<std::pair<std::pair<int, int*>, size_t>, 4, false>(
            [](std::pair<std::pair<int, int*>, size_t>& v, size_t idx) { v.second = idx; },
            [](std::pair<std::pair<int, int*>, size_t>& l, std::pair<std::pair<int, int*>, size_t>& r) { return l.first.first < r.first.first; });

        //              0  1   2   3  4   5  6   7   8
        int nums[] = { 32, 1, 88, 16, 9, 11, 3, 22, 23 };
        for (auto& i : nums)
            q.push(std::make_pair(std::make_pair(i, &i), 0U));
        REQUIRE(q.top().first.first == 1);
        REQUIRE(q.top().first.second == &nums[1]);
        REQUIRE(*q.top().first.second == 1);

        // Update the top element.
        q.top().first.first = 12;
        q.update(1);

        REQUIRE(q.top().first.first == 3);
        REQUIRE(q.top().first.second == &nums[6]);
        q.pop();
        REQUIRE(q.top().first.first == 9);
        REQUIRE(q.top().first.second == &nums[4]);
        q.pop();
        REQUIRE(q.top().first.first == 11);
        REQUIRE(q.top().first.second == &nums[5]);
        q.pop();
        REQUIRE(q.top().first.first == 12);
        REQUIRE(q.top().first.second == &nums[1]);
        q.pop();
        REQUIRE(q.top().first.first == 16);
        REQUIRE(q.top().first.second == &nums[3]);
        q.pop();
        REQUIRE(q.top().first.first == 22);
        REQUIRE(q.top().first.second == &nums[7]);
        q.pop();
        REQUIRE(q.top().first.first == 23);
        REQUIRE(q.top().first.second == &nums[8]);
        q.pop();
        REQUIRE(q.top().first.first == 32);
        REQUIRE(q.top().first.second == &nums[0]);
        q.pop();
        REQUIRE(q.top().first.first == 88);
        REQUIRE(q.top().first.second == &nums[2]);
        q.pop();
        REQUIRE(q.empty());
    }
    SECTION("reschedule to last moves element to correct place", "heap")
    {
        auto q = make_miniheap_mutable_priority_queue<std::pair<std::pair<int, int*>, size_t>, 4, false>(
            [](std::pair<std::pair<int, int*>, size_t>& v, size_t idx) { v.second = idx; },
            [](std::pair<std::pair<int, int*>, size_t>& l, std::pair<std::pair<int, int*>, size_t>& r) { return l.first.first < r.first.first; });

        //              0  1   2   3  4   5  6   7   8
        int nums[] = { 32, 1, 88, 16, 9, 11, 3, 22, 23 };
        for (auto& i : nums)
            q.push(std::make_pair(std::make_pair(i, &i), 0U));
        REQUIRE(q.top().first.first == 1);
        REQUIRE(q.top().first.second == &nums[1]);
        REQUIRE(*q.top().first.second == 1);

        // Update the top element.
        q.top().first.first = 89;
        q.update(1);

        REQUIRE(q.top().first.first == 3);
        REQUIRE(q.top().first.second == &nums[6]);
        q.pop();
        REQUIRE(q.top().first.first == 9);
        REQUIRE(q.top().first.second == &nums[4]);
        q.pop();
        REQUIRE(q.top().first.first == 11);
        REQUIRE(q.top().first.second == &nums[5]);
        q.pop();
        REQUIRE(q.top().first.first == 16);
        REQUIRE(q.top().first.second == &nums[3]);
        q.pop();
        REQUIRE(q.top().first.first == 22);
        REQUIRE(q.top().first.second == &nums[7]);
        q.pop();
        REQUIRE(q.top().first.first == 23);
        REQUIRE(q.top().first.second == &nums[8]);
        q.pop();
        REQUIRE(q.top().first.first == 32);
        REQUIRE(q.top().first.second == &nums[0]);
        q.pop();
        REQUIRE(q.top().first.first == 88);
        REQUIRE(q.top().first.second == &nums[2]);
        q.pop();
        REQUIRE(q.top().first.first == 89);
        REQUIRE(q.top().first.second == &nums[1]);
        q.pop();
        REQUIRE(q.empty());
    }
    SECTION("reschedule top of 2 elements to last") {
        auto q = make_test_priority_queue<8>();
        q.push(std::make_pair(1, 0U));
        q.push(std::make_pair(2, 0U));
        REQUIRE(q.top().first == 1);
        // Update the top element.
        q.top().first = 3;
        q.update(1);
        REQUIRE(q.top().first == 2);
    }
    SECTION("reschedule top of 3 elements left to 2nd") {
        auto q = make_test_priority_queue<8>();
        q.push(std::make_pair(1, 0U));
        q.push(std::make_pair(2, 0U));
        q.push(std::make_pair(4, 0U));
        REQUIRE(q.top().first == 1);
        // Update the top element.
        q.top().first = 3;
        q.update(1);
        REQUIRE(q.top().first == 2);
    }
    SECTION("reschedule top of 3 elements right to 2nd") {
        auto q = make_test_priority_queue<8>();
        q.push(std::make_pair(1, 0U));
        q.push(std::make_pair(4, 0U));
        q.push(std::make_pair(2, 0U));
        REQUIRE(q.top().first == 1);
        // Update the top element.
        q.top().first = 3;
        q.update(1);
        REQUIRE(q.top().first == 2);
    }
    SECTION("reschedule top random gives same resultas pop/push") {
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<unsigned> dist(1, 100000);

        auto pq = make_test_priority_queue<8>();
        std::priority_queue<int, std::vector<int>, std::greater<>> stdq;

        for (size_t outer = 0; outer < 100; ++ outer) {
            int num = gen();
            pq.push(std::make_pair(num, 0U));
            stdq.push(num);
            for (size_t inner = 0; inner < 100; ++ inner) {
                int newval = gen();
                // Update the top element.
                pq.top().first = newval;
                pq.update(1);
                stdq.pop();
                stdq.push(newval);
                auto n  = pq.top().first;
                auto sn = stdq.top();
                REQUIRE(sn == n);
            }
        }
    }
}
