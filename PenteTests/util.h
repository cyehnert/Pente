// Shared helpers for the PenteTests commands. Included once, from main.cpp,
// after the engine source, so they can use the engine's internal state.
#pragma once
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using namespace pente;

static int g_fails = 0;
#define CHECK(c) do { if (!(c)) { printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); g_fails++; } } while (0)

static const char* kLetters = "ABCDEFGHJKLMN";

// "G7" -> 0-based point index; -1 if malformed.
static int Q(const char* s)
{
	const char* c = s[0] ? strchr(kLetters, s[0]) : nullptr;
	int row = atoi(s + 1);
	if (!c || row < 1 || row > N) return -1;
	return (row - 1) * N + (int)(c - kLetters);
}

static std::string nm(int q) { return std::string(1, kLetters[q % N]) + std::to_string(q / N + 1); }

static void put(const char* s, int who) { place(Q(s) + 1, who); }

static double ms_since(std::chrono::steady_clock::time_point t0)
{
	return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
}

// After the computer plays q, can the human force a win (engine search)?
static bool forced_after(int q, int depth)
{
	Snapshot s;
	save(s);
	place_stone(q + 1, Computer);
	bool r = status == InProgress && human_forced_win(depth);
	restore(s);
	return r;
}

// Does the computer playing q let the human win with their next stone?
static bool loses_at_once(int q)
{
	Snapshot s;
	save(s);
	place_stone(q + 1, Computer);
	bool r = status == InProgress && !win_points(Human).empty();
	restore(s);
	return r;
}

// Oracle for the search: like human_forced_win, but the human may try every
// empty point and the computer may answer on every empty point.
static bool oracle_forced(int depth)
{
	if (!win_points(Human).empty()) return true;
	if (depth == 0) return false;
	Snapshot before;
	save(before);
	for (int h = 0; h < N2; h++)
	{
		if (p[h]) continue;
		place_stone(h + 1, Human);
		bool forced = false;
		if (status == HumanWins) forced = true;
		else if (status == InProgress && !win_points(Human).empty())
		{
			forced = true;
			Snapshot mid;
			save(mid);
			for (int c = 0; c < N2 && forced; c++)
			{
				if (p[c]) continue;
				place_stone(c + 1, Computer);
				bool holds = status == ComputerWins || status == Tie || (status == InProgress && !oracle_forced(depth - 1));
				restore(mid);
				if (holds) forced = false;
			}
		}
		restore(before);
		if (forced) return true;
	}
	return false;
}

static bool oracle_after(int q, int depth)
{
	Snapshot s;
	save(s);
	place_stone(q + 1, Computer);
	bool r = status == InProgress && oracle_forced(depth);
	restore(s);
	return r;
}

// A random tactical position: clustered stones, 0-4 pairs held by each side.
static void random_position()
{
	Reset(true);
	int stones = 12 + rand() % 24;
	for (int k = 0; k < stones; k++)
	{
		int q;
		do { q = (3 + rand() % 7) * N + 3 + rand() % 7; } while (p[q]);
		p[q] = (k % 2) ? Computer : Human;
	}
	rebuild();
	captures_h = rand() % 5;
	captures_c = rand() % 5;
	win_sol = -1;
	update_capture_weights();
	plan_valid = false;
}

// A position worth testing: game on, and the computer cannot simply win with
// a five. By default the human must not already have a winning point either;
// the one-move safety test wants those positions (blocking is what it checks).
static bool usable_position(bool allowHumanWinPoint = false)
{
	if (check_win() != InProgress) return false;
	for (int k = 0; k < D; k++) if (sn[k] == 4) return false;
	return allowHumanWinPoint || win_points(Human).empty();
}

// Scripted test opponent: wins if it can, else captures the most pairs, else
// blocks a computer winning point, else a random point next to the stones.
static void scripted_human_move()
{
	std::vector<int> wins = win_points(Human);
	int best = wins.empty() ? -1 : wins[0];
	int bestPairs = 0;
	for (int q = 0; q < N2 && best < 0; q++)
		if (!p[q] && pairs_at(q, Human) > bestPairs) bestPairs = pairs_at(q, Human);
	if (best < 0 && bestPairs)
		for (int q = 0; q < N2 && best < 0; q++)
			if (!p[q] && pairs_at(q, Human) == bestPairs) best = q;
	if (best < 0)
	{
		std::vector<int> threats = win_points(Computer);
		if (!threats.empty()) best = threats[0];
	}
	if (best < 0)
	{
		std::vector<int> cand;
		for (int q = 0; q < N2; q++)
		{
			if (p[q]) continue;
			bool near = false;
			for (int dy = -1; dy <= 1; dy++)
				for (int dx = -1; dx <= 1; dx++)
				{
					int v = stone_at(q % N + dx, q / N + dy);
					if (v == Human || v == Computer) near = true;
				}
			if (near) cand.push_back(q);
		}
		best = cand.empty() ? 6 * N + 6 : cand[rand() % cand.size()];
	}
	HumanMove(best / N, best % N);
}
