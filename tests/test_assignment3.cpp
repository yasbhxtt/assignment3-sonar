// Unit tests for Assignment3.cpp and ThreadPool.h.
//
// The application's source files are included directly so they stay unchanged.
// main() is renamed so the test runner can provide its own. The whole program is
// run separately as the assignment3_smoke test (see CMakeLists.txt).
#define main assignment_main
#include "Assignment3.cpp"
#undef main

#include <gtest/gtest.h>

#include <atomic>
#include <sstream>

namespace {

// Reference implementation: count whitespace-separated words the simple way.
long long reference_word_count(const std::string& text) {
	std::istringstream in(text);
	std::string word;
	long long n = 0;
	while (in >> word) {
		n++;
	}
	return n;
}

std::string repeated_text(int times) {
	std::string text;
	for (int i = 0; i < times; i++) {
		text += "the quick brown fox and Hamlet met Horatio near God\n";
	}
	return text;
}

}  // namespace

// ---------- helpers ----------

TEST(Helpers, IsLetterAndIsNotSpace) {
	EXPECT_TRUE(is_letter('a'));
	EXPECT_TRUE(is_letter('Z'));
	EXPECT_FALSE(is_letter('-'));
	EXPECT_FALSE(is_letter('\''));
	EXPECT_TRUE(is_not_space('x'));
	EXPECT_FALSE(is_not_space(' '));
	EXPECT_FALSE(is_not_space('\n'));
}

TEST(Helpers, AverageLatencyOfNothingIsZero) {
	EXPECT_EQ(average_latency_ms({}, {}), 0.0);
}

TEST(Helpers, AverageLatencyAndMsBetween) {
	Clock::time_point t0 = Clock::now();
	std::vector<Clock::time_point> handed = { t0, t0 };
	std::vector<Clock::time_point> done = { t0 + std::chrono::milliseconds(10), t0 + std::chrono::milliseconds(30) };
	EXPECT_NEAR(average_latency_ms(handed, done), 20.0, 0.001);
	EXPECT_NEAR(ms_between(t0, t0 + std::chrono::milliseconds(5)), 5.0, 0.001);
}

// ---------- counting the four target words ----------

TEST(TargetWords, IgnoresCaseAndPunctuation) {
	std::string text = "Horatio and HAMLET, God! hamlet-god and.";
	Counts c = count_target_words(text, 0, text.size());
	EXPECT_EQ(c[0], 1);  // horatio
	EXPECT_EQ(c[1], 2);  // and
	EXPECT_EQ(c[2], 2);  // hamlet
	EXPECT_EQ(c[3], 2);  // god
}

TEST(TargetWords, DoesNotMatchInsideLongerWords) {
	std::string text = "Hamlets godly android horatios";
	Counts c = count_target_words(text, 0, text.size());
	EXPECT_EQ(c[0] + c[1] + c[2] + c[3], 0);
}

TEST(TargetWords, EmptyTextGivesZeros) {
	Counts c = count_target_words("", 0, 0);
	EXPECT_EQ(c[0] + c[1] + c[2] + c[3], 0);
}

// ---------- cutting the text ----------

TEST(CutPoints, NeverSplitAWord) {
	std::string text = "aaaa bbbb cccc dddd eeee ffff";
	std::vector<size_t> cuts = make_cut_points(text, 4, is_letter);
	ASSERT_EQ(cuts.size(), 5u);
	EXPECT_EQ(cuts.front(), 0u);
	EXPECT_EQ(cuts.back(), text.size());
	for (size_t i = 1; i + 1 < cuts.size(); i++) {
		EXPECT_LE(cuts[i - 1], cuts[i]);
		EXPECT_TRUE(cuts[i] == text.size() || !is_letter(text[cuts[i]]));
	}
}

// ---------- plain threads (requirement 1) ----------

TEST(Threads, ResultDoesNotDependOnThreadCount) {
	std::string text = repeated_text(4000);
	Counts expected = count_target_words(text, 0, text.size());
	for (size_t n : { 1u, 2u, 4u, 8u }) {
		Timing t;
		Counts got = count_words_with_threads(text, n, t);
		EXPECT_EQ(got, expected) << "threads=" << n;
		EXPECT_GE(t.execution_ms, 0.0);
	}
	EXPECT_EQ(expected[0], 4000);
	EXPECT_EQ(expected[2], 4000);
}

// ---------- word total (requirement 2) ----------

TEST(WordCount, SliceCountsWhitespaceSeparatedWords) {
	std::string text = "one two  three\nfour";
	EXPECT_EQ(count_words_in_slice(text, 0, text.size()), 4);
	EXPECT_EQ(count_words_in_slice("", 0, 0), 0);
}

TEST(WordCount, PoolTotalMatchesReference) {
	std::string text = repeated_text(3000);
	for (size_t workers : { 1u, 3u }) {
		ThreadPool pool(workers);
		size_t chunks = 0;
		Timing t;
		EXPECT_EQ(count_total_words_with_pool(text, pool, chunks, t), reference_word_count(text)) << "workers=" << workers;
		EXPECT_GT(chunks, 1u);
	}
}

TEST(WordCount, TinyTextUsesOneChunk) {
	ThreadPool pool(2);
	size_t chunks = 0;
	Timing t;
	EXPECT_EQ(count_total_words_with_pool("just three words", pool, chunks, t), 3);
	EXPECT_EQ(chunks, 1u);
}

// ---------- the thread pool ----------

TEST(ThreadPoolTest, RunsEveryQueuedTaskBeforeShuttingDown) {
	std::atomic<int> done{ 0 };
	{
		ThreadPool pool(4);
		for (int i = 0; i < 200; i++) {
			pool.enqueue([&done] { done++; });
		}
	}  // the destructor lets the workers finish the queue
	EXPECT_EQ(done.load(), 200);
}

TEST(ThreadPoolTest, PoolBuiltWithZeroThreadsStillRunsTasks) {
	// Independently tested risk: a pool asked for 0 threads used to accept tasks
	// and never run them, so anything waiting for a result would wait forever.
	ThreadPool pool(0);
	std::promise<int> answer;
	std::future<int> result = answer.get_future();
	pool.enqueue([&answer] { answer.set_value(42); });
	ASSERT_EQ(result.wait_for(std::chrono::seconds(2)), std::future_status::ready);
	EXPECT_EQ(result.get(), 42);
}

// ---------- the real Hamlet.txt (the test runs inside the Assignment3 folder) ----------

TEST(HamletFile, LoadsAndThreadedCountsMatchReference) {
	std::string text;
	ASSERT_TRUE(load_file(text));
	ASSERT_GT(text.size(), 100000u);

	Timing t;
	Counts threaded = count_words_with_threads(text, 4, t);
	Counts single = count_target_words(text, 0, text.size());
	EXPECT_EQ(threaded, single);
	EXPECT_GT(threaded[2], 0);

	ThreadPool pool(4);
	size_t chunks = 0;
	EXPECT_EQ(count_total_words_with_pool(text, pool, chunks, t), reference_word_count(text));
}
