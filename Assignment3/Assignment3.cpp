#include <iostream>
#include <iomanip>
#include <fstream>
#include <string>
#include <vector>
#include <array>
#include <memory>
#include <thread>
#include <future>
#include <latch>
#include <chrono>
#include <cstddef>
#include <algorithm>
#include <cctype>
#include "ThreadPool.h"

using namespace std;

// the four words we count, written in lower case so the comparison ignores case
const array<string, 4> TARGET_WORDS = { "horatio", "and", "hamlet", "god" };
const array<string, 4> DISPLAY_NAMES = { "Horatio", "and", "Hamlet", "God" };

// one count for each target word, in the same order as TARGET_WORDS
using Counts = array<long long, 4>;

// do not give a thread less than this much text, because starting a thread has a cost
const size_t MIN_BYTES_PER_THREAD = 16 * 1024;

// the thread pool gets the text in pieces of about this size
const size_t CHUNK_BYTES = 8 * 1024;

// how many times each measurement is repeated before taking the average
// (Debug mode is very slow, so it repeats fewer times)
#ifdef _DEBUG
const int MEASURE_RUNS = 20;
#else
const int MEASURE_RUNS = 200;
#endif

using Clock = chrono::steady_clock;

// the two numbers measured for one stage of the program
struct Timing {
	double execution_ms = 0; // time from handing out the first piece of work until every result is ready
	double latency_ms = 0;   // average time from handing out one piece of work until its result is ready
};

double ms_between(Clock::time_point from, Clock::time_point to) {
	return chrono::duration<double, milli>(to - from).count();
}

double average_latency_ms(const vector<Clock::time_point>& handed_out, const vector<Clock::time_point>& finished) {
	if (handed_out.empty()) {
		return 0;
	}
	double sum = 0;
	for (size_t i = 0; i < handed_out.size(); i++) {
		sum += ms_between(handed_out[i], finished[i]);
	}
	return sum / handed_out.size();
}

bool is_letter(char c) {
	return isalpha((unsigned char)c) != 0;
}

bool is_not_space(char c) {
	return isspace((unsigned char)c) == 0;
}

// Loads the whole file into one string. The file must be inside the project folder,
// so we look in the folder the program runs from and a few folders around it.
bool load_file(string& text) {
	const vector<string> places = {
		"Hamlet.txt",
		"../Hamlet.txt",
		"../../Hamlet.txt",
		"Assignment3/Hamlet.txt",
		"../Assignment3/Hamlet.txt",
		"../../Assignment3/Hamlet.txt"
	};

	for (const string& place : places) {
		ifstream file(place, ios::binary);
		if (file) {
			text.assign((istreambuf_iterator<char>(file)), istreambuf_iterator<char>());
			return true;
		}
	}

	cout << "Could not find Hamlet.txt. Place it in the project folder next to Assignment3.vcxproj." << endl;
	cout << "These places were checked:" << endl;
	for (const string& place : places) {
		cout << "  " << place << endl;
	}
	return false;
}

// Cuts the text into 'parts' slices. Each cut is moved forward until it is no longer
// in the middle of a word, so a word is never split between two workers.
// keep_together says which characters belong to a word.
vector<size_t> make_cut_points(const string& text, size_t parts, bool (*keep_together)(char)) {
	vector<size_t> cuts;
	cuts.push_back(0);

	for (size_t i = 1; i < parts; i++) {
		size_t cut = text.size() * i / parts;
		while (cut < text.size() && keep_together(text[cut])) {
			cut++;
		}
		if (cut < cuts.back()) {
			cut = cuts.back();
		}
		cuts.push_back(cut);
	}

	cuts.push_back(text.size());
	return cuts;
}

// ---------- Requirement 1: count the four words with ordinary threads ----------

// Checks one word (a run of letters) against the four target words, ignoring case.
void check_word(const string& text, size_t start, size_t length, Counts& counts) {
	for (size_t w = 0; w < TARGET_WORDS.size(); w++) {
		const string& target = TARGET_WORDS[w];
		if (length != target.size()) {
			continue;
		}

		bool same = true;
		for (size_t k = 0; k < length; k++) {
			if (tolower((unsigned char)text[start + k]) != target[k]) {
				same = false;
				break;
			}
		}
		if (same) {
			counts[w]++;
		}
	}
}

// Counts the four target words inside one slice of the text, from begin up to end.
// A word is a run of letters, so spaces, punctuation, hyphens and apostrophes all end a word.
Counts count_target_words(const string& text, size_t begin, size_t end) {
	Counts counts = { 0, 0, 0, 0 };
	bool in_word = false;
	size_t word_start = begin;

	for (size_t i = begin; i <= end; i++) {
		bool letter = (i < end) && is_letter(text[i]);

		if (letter && !in_word) {
			in_word = true;
			word_start = i;
		}
		else if (!letter && in_word) {
			in_word = false;
			check_word(text, word_start, i - word_start, counts);
		}
	}
	return counts;
}

// One slice of text per thread (no thread pool).
// Each thread sends its counts back to the main thread through a promise and future.
// Each thread also writes the time it finished into its own spot in 'finished'.
Counts count_words_with_threads(const string& text, size_t numThreads, Timing& timing) {
	Clock::time_point start = Clock::now();

	vector<size_t> cuts = make_cut_points(text, numThreads, is_letter);

	vector<thread> threads;
	vector<future<Counts>> results;
	vector<Clock::time_point> handed_out(numThreads); // when each slice was handed to a thread
	vector<Clock::time_point> finished(numThreads);   // when each thread had its result ready

	for (size_t i = 0; i < numThreads; i++) {
		promise<Counts> answer;
		results.push_back(answer.get_future());

		size_t begin = cuts[i];
		size_t end = cuts[i + 1];
		handed_out[i] = Clock::now();
		threads.emplace_back([&text, &finished, i, begin, end, answer = move(answer)]() mutable {
			Counts counts = count_target_words(text, begin, end);
			finished[i] = Clock::now();
			answer.set_value(counts);
			});
	}

	// the main thread waits here for each thread's answer, then adds them together
	Counts total = { 0, 0, 0, 0 };
	for (future<Counts>& result : results) {
		Counts part = result.get();
		for (size_t w = 0; w < total.size(); w++) {
			total[w] += part[w];
		}
	}

	// every thread is finished and joined before this function returns
	for (thread& t : threads) {
		t.join();
	}

	timing.execution_ms = ms_between(start, Clock::now());
	timing.latency_ms = average_latency_ms(handed_out, finished);
	return total;
}

// ---------- Requirements 2 and 4: count all the words with the ThreadPool ----------

// Counts the words in one slice. A word is a group of characters separated by white space,
// which is the same number any word counter reports.
long long count_words_in_slice(const string& text, size_t begin, size_t end) {
	long long words = 0;
	bool in_word = false;

	for (size_t i = begin; i < end; i++) {
		bool part_of_word = is_not_space(text[i]);
		if (part_of_word && !in_word) {
			words++;
		}
		in_word = part_of_word;
	}
	return words;
}

// The text is cut into chunks and every chunk becomes one task for the pool.
// There are more chunks than workers, so a worker that finishes early takes the next chunk.
// Each task writes only to its own spot in chunk_totals and finished, so tasks never share a variable.
long long count_total_words_with_pool(const string& text, ThreadPool& pool, size_t& chunksUsed, Timing& timing) {
	Clock::time_point start = Clock::now();

	size_t numChunks = max<size_t>(1, (text.size() + CHUNK_BYTES - 1) / CHUNK_BYTES);
	chunksUsed = numChunks;

	vector<size_t> cuts = make_cut_points(text, numChunks, is_not_space);
	vector<long long> chunk_totals(numChunks, 0);
	vector<Clock::time_point> handed_out(numChunks); // when each chunk was put in the pool's queue
	vector<Clock::time_point> finished(numChunks);   // when each chunk's result was ready

	// the latch is shared with the tasks so it stays alive until the last worker is done with it
	shared_ptr<latch> all_done = make_shared<latch>((ptrdiff_t)numChunks);

	for (size_t i = 0; i < numChunks; i++) {
		size_t begin = cuts[i];
		size_t end = cuts[i + 1];
		handed_out[i] = Clock::now();
		pool.enqueue([&text, &chunk_totals, &finished, all_done, i, begin, end]() {
			chunk_totals[i] = count_words_in_slice(text, begin, end);
			finished[i] = Clock::now();
			all_done->count_down();
			});
	}

	// the main thread waits here until every chunk has been counted
	all_done->wait();

	long long total = 0;
	for (long long part : chunk_totals) {
		total += part;
	}

	timing.execution_ms = ms_between(start, Clock::now());
	timing.latency_ms = average_latency_ms(handed_out, finished);
	return total;
}

// ---------- Requirement 5: measurements ----------

// Runs the thread version several times and averages the timings.
Timing measure_threads(const string& text, size_t numThreads) {
	Timing one_run;
	count_words_with_threads(text, numThreads, one_run); // warm-up run, not counted

	Timing average;
	for (int r = 0; r < MEASURE_RUNS; r++) {
		count_words_with_threads(text, numThreads, one_run);
		average.execution_ms += one_run.execution_ms;
		average.latency_ms += one_run.latency_ms;
	}
	average.execution_ms /= MEASURE_RUNS;
	average.latency_ms /= MEASURE_RUNS;
	return average;
}

// Runs the pool version several times on a pool that already exists, and averages the timings.
// The pool's threads are created once and reused, so creating them is not part of each run.
Timing measure_pool(const string& text, ThreadPool& pool) {
	Timing one_run;
	size_t chunks = 0;
	count_total_words_with_pool(text, pool, chunks, one_run); // warm-up run, not counted

	Timing average;
	for (int r = 0; r < MEASURE_RUNS; r++) {
		count_total_words_with_pool(text, pool, chunks, one_run);
		average.execution_ms += one_run.execution_ms;
		average.latency_ms += one_run.latency_ms;
	}
	average.execution_ms /= MEASURE_RUNS;
	average.latency_ms /= MEASURE_RUNS;
	return average;
}

// Speedup = time with one worker / time with several workers.
// Efficiency = speedup / number of workers.
void print_report(const string& title, const string& worker, size_t workers, const Timing& one, const Timing& many) {
	double speedup = (many.execution_ms > 0) ? one.execution_ms / many.execution_ms : 0;
	double efficiency = speedup / workers * 100.0;

	cout << title << endl;
	cout << fixed << setprecision(3);
	cout << "  Execution time with 1 " << worker << ":   " << one.execution_ms << " ms" << endl;
	cout << "  Execution time with " << workers << " " << worker << "s: " << many.execution_ms << " ms" << endl;
	cout << "  Latency (average time for one piece of work, " << workers << " " << worker << "s): " << many.latency_ms << " ms" << endl;
	cout << setprecision(2);
	cout << "  Speedup (time with 1 / time with " << workers << "):  " << speedup << "x" << endl;
	cout << setprecision(1);
	cout << "  Efficiency (speedup / " << workers << " " << worker << "s):  " << efficiency << " %" << endl;
}

int main() {
	string text;
	if (!load_file(text)) {
		return 1;
	}

	unsigned int cores = thread::hardware_concurrency();
	if (cores == 0) {
		cores = 4;
	}
	size_t numThreads = min<size_t>(cores, max<size_t>(1, text.size() / MIN_BYTES_PER_THREAD));

	Timing first_run;

	// Requirement 1 runs first. This call does not return until every thread has finished.
	Counts counts = count_words_with_threads(text, numThreads, first_run);

	for (size_t w = 0; w < counts.size(); w++) {
		cout << counts[w] << " " << DISPLAY_NAMES[w] << endl;
	}

	// Requirement 2 starts only now, after all four word counts are done (requirement 3).
	size_t chunks = 0;
	long long totalWords = 0;
	{
		ThreadPool pool(numThreads);
		totalWords = count_total_words_with_pool(text, pool, chunks, first_run);
	}

	cout << "Total word count = " << totalWords << endl;

	// Requirement 5: measure each stage with 1 worker and with numThreads workers.
	cout << endl;
	cout << "Performance (average of " << MEASURE_RUNS << " runs, " << cores << " cores available)" << endl;

	Timing threads_one = measure_threads(text, 1);
	Timing threads_many = measure_threads(text, numThreads);
	print_report("Requirement 1: four word counts with plain threads", "thread", numThreads, threads_one, threads_many);

	cout << endl;

	ThreadPool pool_one(1);
	ThreadPool pool_many(numThreads);
	Timing pool_time_one = measure_pool(text, pool_one);
	Timing pool_time_many = measure_pool(text, pool_many);
	print_report("Requirement 2: total word count with the ThreadPool (" + to_string(chunks) + " chunks)", "worker", numThreads, pool_time_one, pool_time_many);
}