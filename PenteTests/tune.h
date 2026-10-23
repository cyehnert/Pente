// Tuning engine values by self-play.
//
//   tune        capture weights, in games with capture rules (the recorded
//               human games are used as a check)
//   tune-lines  line weights (updat_w), in games without capture rules
//
// Each round tries every tunable value at x1.5 and /1.5. A change is kept
// only if (1) it scores at least kAccept against the current values over the
// training openings, (2) it still scores at least kConfirm on a fresh set of
// openings, and (3) for capture games, it does not lose more of the
// human-game test positions.
#pragma once
#include <cmath>
#include "games.h"
#include "selfplay.h"

struct ParamInfo
{
	const char* name;
	int& (*field)(Player&);
};

#define CAP(f)  { #f, [](Player& pl) -> int& { return pl.cap.f; } }
#define LINE(f) { #f, [](Player& pl) -> int& { return pl.line.f; } }

static const ParamInfo kCaptureTunable[] = {
	CAP(block_win), CAP(capture), CAP(block), CAP(per_capture), CAP(threat), CAP(expose),
	CAP(break_four), CAP(break_three), CAP(break_two), CAP(keep_three), CAP(keep_two),
};

// own4 (completing five) stays fixed: it must always win.
static const ParamInfo kLineTunable[] = {
	LINE(opp4), LINE(opp3), LINE(opp2), LINE(opp1), LINE(vacant), LINE(own1), LINE(own2), LINE(own3),
	LINE(opp3_twice), LINE(own3_twice),
};

static const double kAccept = 0.56;
static const double kConfirm = 0.53;

template <size_t K>
static void print_values(const ParamInfo (&table)[K], Player pl)
{
	for (const ParamInfo& pi : table) printf("    %-12s %6d\n", pi.name, pi.field(pl));
}

// A computer-to-move position from a game the human won with capture rules,
// where some move holds against the deep search.
struct HumanPosition
{
	const GameLog* game;
	int move;
	int played;
	bool playedLoses;     // the move actually played loses by force
};

static void load_position(const HumanPosition& hp)
{
	replay(*hp.game, [&](int num, int) { return num != hp.move; });
}

static std::vector<HumanPosition> collect_human_positions(const std::vector<GameLog>& logs)
{
	std::vector<HumanPosition> out;
	DeepSearch deep;
	for (const GameLog& g : logs)
	{
		if (!g.captureRules) continue;
		replay(g);
		if (Status() != HumanWins) continue;
		std::vector<std::pair<int, int>> turns;
		replay(g, [&](int num, int played) { turns.push_back({ num, played }); return true; });
		for (auto& t : turns)
		{
			HumanPosition hp{ &g, t.first, t.second, false };
			load_position(hp);
			if (deep_safe_move() < 0) continue;   // already lost: nothing to learn
			hp.playedLoses = deep_loses(hp.played);
			out.push_back(hp);
		}
	}
	return out;
}

// Positions where the computer, playing with pl, chooses a move that loses by force.
static int human_position_losses(const std::vector<HumanPosition>& positions, const Player& pl, bool verbose = false)
{
	int losses = 0;
	for (const HumanPosition& hp : positions)
	{
		load_position(hp);
		int q = choose_for(Computer, pl);
		DeepSearch deep;
		if (q >= 0 && deep_loses(q))
		{
			losses++;
			if (verbose) printf("      %s move %d: plays %s, which loses by force\n", hp.game->name.c_str(), hp.move, nm(q).c_str());
		}
	}
	return losses;
}

template <size_t K>
static Player run_tuner(const ParamInfo (&table)[K], bool captures, int rounds, int openingCount, const std::vector<GameLog>& logs)
{
	auto t0 = std::chrono::steady_clock::now();
	Player start = engine_player(captures), cur = start;
	std::vector<Opening> train = make_openings(openingCount, 1001);
	std::vector<Opening> confirm = make_openings(openingCount, 2002);

	std::vector<HumanPosition> positions;
	int curLosses = 0;
	if (captures)
	{
		printf("Collecting human-game test positions...\n");
		positions = collect_human_positions(logs);
		curLosses = human_position_losses(positions, cur, true);
		int playedLosses = 0;
		for (auto& hp : positions) playedLosses += hp.playedLoses;
		printf("  %zu positions; the logged moves lost by force in %d, the current engine loses %d\n\n",
			positions.size(), playedLosses, curLosses);
	}

	printf("Self-play, capture rules %s: %zu training and %zu confirmation openings, each played twice (sides swapped)\n\n",
		captures ? "on" : "off", train.size(), confirm.size());
	for (int round = 1; round <= rounds; round++)
	{
		int accepted = 0;
		printf("Round %d (%.0f s so far)\n", round, ms_since(t0) / 1000);
		for (const ParamInfo& pi : table)
		{
			for (double f : { 1.5, 1 / 1.5 })
			{
				Player cand = cur;
				int old = pi.field(cur);
				int v = (int)std::lround(old * f);
				if (v == old) v = f > 1 ? old + 1 : old - 1;
				if (v < 0) continue;
				pi.field(cand) = v;
				MatchResult m = play_match(cand, cur, train, captures);
				printf("  %-12s %6d -> %-6d  %3d-%3d-%-3d (%.1f%%)", pi.name, old, v, m.aWins, m.bWins, m.ties, 100 * m.score());
				if (m.score() < kAccept) { printf("\n"); continue; }
				MatchResult c = play_match(cand, cur, confirm, captures);
				printf("  confirm %3d-%3d-%-3d (%.1f%%)", c.aWins, c.bWins, c.ties, 100 * c.score());
				if (c.score() < kConfirm) { printf("  rejected\n"); continue; }
				if (captures)
				{
					int losses = human_position_losses(positions, cand);
					printf("  human positions lost %d", losses);
					if (losses > curLosses) { printf("  rejected\n"); continue; }
					curLosses = losses;
				}
				printf("  ACCEPTED\n");
				cur = cand;
				accepted++;
				break;   // the other direction cannot also be better
			}
		}
		if (!accepted) { printf("No change improved on the current values; stopping.\n\n"); break; }
		printf("\n");
	}

	std::vector<Opening> fresh = make_openings(openingCount, 3003);
	MatchResult fin = play_match(cur, start, fresh, captures);
	printf("Final values against the starting values on fresh openings: %d-%d-%d (%.1f%%)",
		fin.aWins, fin.bWins, fin.ties, 100 * fin.score());
	if (captures) printf(", human positions lost %d (starting values %d)", curLosses, human_position_losses(positions, start));
	printf("\nFinal values:\n");
	print_values(table, cur);
	printf("Total time %.0f s\n", ms_since(t0) / 1000);
	return cur;
}
