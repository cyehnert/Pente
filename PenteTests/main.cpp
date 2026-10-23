// PenteTests: tests, game analysis and parameter tuning for the Pente engine.
//
//   PenteTests test                    run the engine tests
//   PenteTests analyze [files...]      replay game logs (default: all in games\)
//                                      and find where the computer went wrong
//   PenteTests explain <file> <move>   the computer's candidates before one of its moves
//   PenteTests match [openings]        current values against themselves (sanity),
//                                      and other search settings against the live ones
//   PenteTests tune [rounds] [openings]
//                                      tune the capture weights (capture games)
//   PenteTests tune-lines [rounds] [openings]
//                                      tune the line weights (games without captures)
//
// The engine source is compiled into this program directly so the tests can
// reach its internal state.
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include "../PenteGUI/pente_engine.cpp"
#include "util.h"
#include "games.h"
#include "selfplay.h"
#include "tune.h"
#include "tests.h"

static int usage()
{
	printf("usage: PenteTests test | analyze [files...] | explain <file> <move> | match [openings] | tune [rounds] [openings] | tune-lines [rounds] [openings]\n");
	return 2;
}

int main(int argc, char** argv)
{
	setvbuf(stdout, nullptr, _IONBF, 0);   // progress shows up immediately
	if (argc < 2) return usage();
	std::string cmd = argv[1];

	if (cmd == "test") return run_tests();

	if (cmd == "analyze")
	{
		std::vector<GameLog> logs;
		if (argc > 2)
			for (int i = 2; i < argc; i++) logs.push_back(read_log(argv[i]));
		else
		{
			printf("Reading %s\n", games_dir().c_str());
			logs = read_all_logs(games_dir());
		}
		for (const GameLog& g : logs)
		{
			if (!g.error.empty()) { printf("=== %s: %s\n", g.name.c_str(), g.error.c_str()); continue; }
			analyze_game(g, false);
		}
		return 0;
	}

	if (cmd == "explain" && argc > 3)
	{
		GameLog g = read_log(argv[2]);
		int move = atoi(argv[3]);
		if (!g.error.empty()) { printf("%s: %s\n", g.name.c_str(), g.error.c_str()); return 1; }
		int logged = -1;
		replay(g, [&](int num, int played) { if (num == move) logged = played; return num != move; });
		if (logged < 0) { printf("move %d is not a computer move in %s\n", move, g.name.c_str()); return 1; }
		printf("%s, before computer move %d (logged: %s)\n\n%s\npairs held: H %d, X %d\n\n",
			g.name.c_str(), move, nm(logged).c_str(), BoardText().c_str(), captures_h, captures_c);
		int pr, pc;
		PlannedMove(pr, pc);
		std::vector<int> order;
		for (int i = 0; i < N2; i++) if (!p[i]) order.push_back(i);
		std::stable_sort(order.begin(), order.end(), [](int x, int y) { return total_weight(x) > total_weight(y); });
		printf("  point   total   line  capture   verdict\n");
		for (int i = 0; i < 12 && i < (int)order.size(); i++)
		{
			int q = order[i];
			const char* verdict;
			if (loses_at_once(q)) verdict = "loses at once";
			else
			{
				force_nodes = 0;
				bool live = forced_after(q, force_depth);
				DeepSearch deep;
				verdict = live ? "forced loss" : deep_loses(q) ? "forced loss (deep search only)" : "holds";
			}
			printf("  %-5s %7d %6d %+8d   %s%s%s\n", nm(q).c_str(), total_weight(q), w[q], cw[q], verdict,
				q == pr * N + pc ? "   <- engine plays" : "", q == logged ? "   <- logged" : "");
		}
		return 0;
	}

	if (cmd == "match")
	{
		int count = argc > 2 ? atoi(argv[2]) : 100;
		std::vector<Opening> openings = make_openings(count, 4004);
		Player d = engine_player(true);
		auto t0 = std::chrono::steady_clock::now();
		MatchResult same = play_match(d, d, openings, true);
		printf("defaults vs defaults:  %d-%d-%d (%.1f%%), %.1f s\n", same.aWins, same.bWins, same.ties, 100 * same.score(), ms_since(t0) / 1000);
		// Search settings compared with the same capture values: side A uses
		// (depthA, budgetA), side B the live settings (3, 20000). Depth 0 means
		// the one-move check only.
		auto searchMatch = [&](int depthA, long budgetA, double& slowestA)
		{
			MatchResult m;
			slowestA = 0;
			for (const Opening& o : openings)
				for (int aIsX = 0; aIsX <= 1; aIsX++)
				{
					Reset(true);
					Owner side = o.first;
					for (int q : o.points) { place(q + 1, side); side = side == Computer ? Human : Computer; }
					while (status == InProgress && history.size() < (size_t)N2)
					{
						bool sideIsA = (side == Computer) == (aIsX == 1);
						force_depth = sideIsA ? depthA : 3;
						force_budget = sideIsA ? budgetA : 20000;
						auto t1 = std::chrono::steady_clock::now();
						int q = choose_for(side, d);
						if (sideIsA) slowestA = std::max(slowestA, ms_since(t1));
						if (q < 0) { status = Tie; break; }
						place(q + 1, side);
						side = side == Computer ? Human : Computer;
					}
					force_depth = 3;
					force_budget = 20000;
					bool aWon = (status == ComputerWins) == (aIsX == 1) && status != Tie && status != InProgress;
					bool bWon = !aWon && (status == ComputerWins || status == HumanWins);
					if (aWon) m.aWins++; else if (bWon) m.bWins++; else m.ties++;
				}
			return m;
		};
		struct { int depth; long budget; const char* label; } settings[] = {
			{ 0, 20000, "no search (one-move check only)" },
			{ 4, 60000, "depth 4, budget 60000" },
			{ 5, 150000, "depth 5, budget 150000" },
		};
		for (auto& st : settings)
		{
			double slowest;
			auto t1 = std::chrono::steady_clock::now();
			MatchResult m = searchMatch(st.depth, st.budget, slowest);
			printf("%-32s vs live search: %d-%d-%d (%.1f%%), slowest move %.0f ms, %.0f s\n",
				st.label, m.aWins, m.bWins, m.ties, 100 * m.score(), slowest, ms_since(t1) / 1000);
		}
		return 0;
	}

	if (cmd == "search-plain")
	{
		// Games without captures: side A also uses the safety check and the
		// forcing search, side B chooses by weight alone (the original way).
		int count = argc > 2 ? atoi(argv[2]) : 150;
		std::vector<Opening> openings = make_openings(count, 5005);
		Player d = engine_player(false);
		MatchResult m;
		double slowest = 0;
		auto t0 = std::chrono::steady_clock::now();
		for (const Opening& o : openings)
			for (int aIsX = 0; aIsX <= 1; aIsX++)
			{
				Reset(false);
				Owner side = o.first;
				for (int q : o.points) { place(q + 1, side); side = side == Computer ? Human : Computer; }
				while (status == InProgress && history.size() < (size_t)N2)
				{
					bool sideIsA = (side == Computer) == (aIsX == 1);
					search_without_captures = sideIsA;
					auto t1 = std::chrono::steady_clock::now();
					int q = choose_for(side, d);
					if (sideIsA) slowest = std::max(slowest, ms_since(t1));
					if (q < 0) { status = Tie; break; }
					place(q + 1, side);
					side = side == Computer ? Human : Computer;
				}
				search_without_captures = true;
				bool aWon = (status == ComputerWins) == (aIsX == 1) && status != Tie && status != InProgress;
				bool bWon = !aWon && (status == ComputerWins || status == HumanWins);
				if (aWon) m.aWins++; else if (bWon) m.bWins++; else m.ties++;
			}
		printf("no captures, search vs weights alone: %d-%d-%d (%.1f%%), slowest move %.0f ms, %.0f s\n",
			m.aWins, m.bWins, m.ties, 100 * m.score(), slowest, ms_since(t0) / 1000);
		return 0;
	}

	if (cmd == "quiet-match")
	{
		// Side A uses the quiet-threat check, side B does not; with and without
		// capture rules.
		int count = argc > 2 ? atoi(argv[2]) : 150;
		std::vector<Opening> openings = make_openings(count, 6006);
		for (bool captures : { true, false })
		{
			Player d = engine_player(captures);
			MatchResult m;
			double slowest = 0;
			auto t0 = std::chrono::steady_clock::now();
			for (const Opening& o : openings)
				for (int aIsX = 0; aIsX <= 1; aIsX++)
				{
					Reset(captures);
					Owner side = o.first;
					for (int q : o.points) { place(q + 1, side); side = side == Computer ? Human : Computer; }
					while (status == InProgress && history.size() < (size_t)N2)
					{
						bool sideIsA = (side == Computer) == (aIsX == 1);
						use_quiet_check = sideIsA;
						auto t1 = std::chrono::steady_clock::now();
						int q = choose_for(side, d);
						if (sideIsA) slowest = std::max(slowest, ms_since(t1));
						if (q < 0) { status = Tie; break; }
						place(q + 1, side);
						side = side == Computer ? Human : Computer;
					}
					use_quiet_check = true;
					bool aWon = (status == ComputerWins) == (aIsX == 1) && status != Tie && status != InProgress;
					bool bWon = !aWon && (status == ComputerWins || status == HumanWins);
					if (aWon) m.aWins++; else if (bWon) m.bWins++; else m.ties++;
				}
			printf("captures %-3s: quiet check vs without: %d-%d-%d (%.1f%%), slowest move %.0f ms, %.0f s\n",
				captures ? "on" : "off", m.aWins, m.bWins, m.ties, 100 * m.score(), slowest, ms_since(t0) / 1000);
		}
		return 0;
	}

	if (cmd == "tune")
	{
		int rounds = argc > 2 ? atoi(argv[2]) : 3;
		int count = argc > 3 ? atoi(argv[3]) : 100;
		std::vector<GameLog> logs = read_all_logs(games_dir(), true);
		run_tuner(kCaptureTunable, true, rounds, count, logs);
		return 0;
	}

	if (cmd == "tune-lines")
	{
		int rounds = argc > 2 ? atoi(argv[2]) : 6;
		int count = argc > 3 ? atoi(argv[3]) : 150;
		run_tuner(kLineTunable, false, rounds, count, {});
		return 0;
	}

	return usage();
}
