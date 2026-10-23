// Engine tests: "PenteTests test".
#pragma once
#include "games.h"
#include "selfplay.h"

static void test_rebuild_matches_incremental()
{
	// The rebuild used after captures (and by board mirroring) must give exactly
	// the state the original incremental updates reach.
	srand(7);
	int mismatches = 0;
	for (int game = 0; game < 100; game++)
	{
		Reset(false);
		int who = (game % 2) ? Computer : Human;
		while (Status() == InProgress)
		{
			int r, c;
			if (who == Computer) ComputerMove(r, c);
			else { do { r = rand() % N; c = rand() % N; } while (Cell(r, c) != Empty); HumanMove(r, c); }
			who = who == Computer ? Human : Computer;
			if (Status() != InProgress) break;
			int sn0[D], w0[N2], cs0[E];
			memcpy(sn0, sn, sizeof sn); memcpy(w0, w, sizeof w); memcpy(cs0, cs, sizeof cs);
			int h0 = ncapt_h, c0 = ncapt_c;
			rebuild();
			if (memcmp(sn0, sn, sizeof sn) || memcmp(w0, w, sizeof w) || memcmp(cs0, cs, sizeof cs) || h0 != ncapt_h || c0 != ncapt_c)
			{
				mismatches++;
				break;
			}
		}
	}
	CHECK(mismatches == 0);
}

static void test_capture_rules()
{
	auto at = [](const char* s) { return Cell(Q(s) / N, Q(s) % N); };

	Reset(true);   // basic capture
	put("G7", Computer); put("H7", Human); put("J7", Human); put("K7", Computer);
	CHECK(at("H7") == Empty && at("J7") == Empty && Captures(Computer) == 1 && history.back().captured.size() == 2);

	Reset(false);  // rules off: nothing removed
	put("G7", Computer); put("H7", Human); put("J7", Human); put("K7", Computer);
	CHECK(at("H7") == Human && at("J7") == Human && Captures(Computer) == 0);

	Reset(true);   // moving into a bracket is safe
	put("G7", Computer); put("H7", Human); put("K7", Computer); put("J7", Human);
	CHECK(at("H7") == Human && at("J7") == Human);

	Reset(true);   // two directions at once
	put("D4", Human); put("E5", Computer); put("F6", Computer);
	put("G4", Human); put("G5", Computer); put("G6", Computer);
	put("G7", Human);
	CHECK(Captures(Human) == 2 && at("E5") == Empty && at("F6") == Empty && at("G5") == Empty && at("G6") == Empty);

	Reset(true);   // a capture reopens a blocked line
	put("A1", Human); put("B1", Computer); put("C1", Computer); put("D1", Human);
	bool reopened = false;
	for (int k = 0; k < D; k++) if (a[k][0] == Q("A1") + 1 && a[k][4] == Q("E1") + 1) reopened = sn[k] == -2;
	CHECK(reopened);

	Reset(true);   // five captures win
	const char* rows[] = { "1", "3", "5", "7", "9" };
	for (const char* r : rows)
	{
		std::string a1 = std::string("A") + r, b1 = std::string("B") + r, c1 = std::string("C") + r, d1 = std::string("D") + r;
		put(a1.c_str(), Computer); put(b1.c_str(), Human); put(c1.c_str(), Human); put(d1.c_str(), Computer);
	}
	int lr[5], lc[5];
	CHECK(Status() == ComputerWins && WonByCaptures() && !WinningLine(lr, lc));

	Reset(true);   // five in a row still wins
	for (const char* s : { "A13", "B13", "C13", "D13", "E13" }) put(s, Human);
	CHECK(Status() == HumanWins && !WonByCaptures() && WinningLine(lr, lc));

	Reset(true);   // undo replays captures
	{
		int r, c;
		HumanMove(6, 6); ComputerMove(r, c);
		HumanMove(6, 7); ComputerMove(r, c);
		HumanMove(5, 5); ComputerMove(r, c);
		size_t n = history.size();
		int board[N2];
		memcpy(board, p, sizeof p);
		int cc = Captures(Computer), hc = Captures(Human);
		HumanMove(0, 0); ComputerMove(r, c);
		CHECK(Undo() && history.size() == n && !memcmp(board, p, sizeof p) && cc == Captures(Computer) && hc == Captures(Human));
	}
}

static void test_capture_weights()
{
	auto planned = []() { int r, c; PlannedMove(r, c); return r * N + c; };

	Reset(false);  // no capture weights without capture rules
	put("G7", Computer); put("H7", Human); put("J7", Human);
	bool zero = true;
	for (int i = 0; i < N2; i++) if (cw[i]) zero = false;
	CHECK(zero);

	Reset(true);   // takes a capture
	put("G7", Computer); put("H7", Human); put("J7", Human);
	CHECK(planned() == Q("K7"));

	Reset(true);   // saves a threatened pair
	put("G7", Computer); put("H7", Computer); put("F7", Human);
	CHECK(planned() == Q("J7"));

	Reset(true);   // exposing a pair is penalised
	put("G7", Computer); put("F7", Human);
	CHECK(cw[Q("H7")] < 0);

	Reset(true);   // the fifth capture beats blocking a four
	captures_c = 4;
	put("A1", Human); put("B1", Human); put("C1", Human); put("D1", Human);
	put("K10", Computer); put("K9", Human); put("K8", Human);
	update_capture_weights();
	plan_valid = false;
	CHECK(planned() == Q("K7"));

	Reset(true);   // blocks the human's fifth capture
	captures_h = 4;
	put("G7", Computer); put("H7", Computer); put("F7", Human);
	update_capture_weights();
	plan_valid = false;
	CHECK(planned() == Q("J7"));
}

static void test_safety_check()
{
	// Step-2 behaviour (depth 0): skip a move only if it loses at once, and
	// only when a move that does not lose at once exists.
	int saved = force_depth;
	force_depth = 0;
	srand(5);
	int positions = 0, saves = 0;
	for (int trial = 0; trial < 3000; trial++)
	{
		random_position();
		if (!usable_position(true)) continue;
		positions++;
		int top = choose_pt() - 1, plan = computer_pt() - 1;
		if (!loses_at_once(top)) { CHECK(plan == top); continue; }
		bool anySafe = false;
		for (int q = 0; q < N2 && !anySafe; q++) if (!p[q] && !loses_at_once(q)) anySafe = true;
		if (anySafe)
		{
			saves++;
			CHECK(!loses_at_once(plan));
			for (const Point& r : RejectedMoves()) CHECK(loses_at_once(r.row * N + r.col));
		}
		else CHECK(plan == top && RejectedMoves().empty());
	}
	force_depth = saved;
	printf("  %d random positions, %d saved by the one-move check\n", positions, saves);
	CHECK(saves > 0);
}

static void test_search()
{
	auto planned = []() { int r, c; PlannedMove(r, c); return r * N + c; };

	// capture threat + open three: block the three, not the pair
	Reset(true);
	put("G6", Computer); put("E7", Human); put("G5", Computer); put("F7", Human);
	put("K10", Computer); put("G7", Human);
	CHECK(forced_after(Q("G4"), 3));
	int plan = planned();
	CHECK(plan != Q("G4") && !forced_after(plan, 3));

	// the first recorded game: moves 20 and 32
	std::string first = games_dir() + "\\10-21-2026_39moves_pente_board.txt";
	GameLog g = read_log(first);
	if (g.error.empty())
	{
		auto at = [&](int move) { replay(g, [&](int num, int) { return num != move; }); };
		at(20);
		CHECK(forced_after(Q("E2"), 3));
		CHECK(!forced_after(planned(), 3));
		at(32);
		CHECK(forced_after(Q("E7"), 3) && forced_after(Q("H4"), 3));
		CHECK(planned() == Q("J8"));
	}
	else printf("  (recorded game not found; skipped)\n");

	// soundness: forced-loss skips confirmed by the full-width oracle (depth 2)
	int savedDepth = force_depth;
	force_depth = 2;
	srand(17);
	int verified = 0;
	for (int trial = 0; trial < 4000 && verified < 15; trial++)
	{
		random_position();
		if (!usable_position()) continue;
		computer_pt();
		for (const Point& r : RejectedMoves())
		{
			int q = r.row * N + r.col;
			if (loses_at_once(q) || verified >= 15) continue;
			verified++;
			CHECK(oracle_after(q, 2));
		}
	}
	force_depth = savedDepth;
	CHECK(verified == 15);

	// speed
	srand(23);
	double worst = 0, total = 0;
	int plans = 0;
	for (int trial = 0; trial < 1500; trial++)
	{
		random_position();
		if (!usable_position()) continue;
		auto t0 = std::chrono::steady_clock::now();
		computer_pt();
		double ms = ms_since(t0);
		worst = std::max(worst, ms); total += ms; plans++;
	}
	printf("  %d plans, average %.1f ms, slowest %.0f ms\n", plans, total / plans, worst);
	CHECK(worst < 600);
}

static void test_no_capture_games()
{
	// Without capture rules the computer plays what the original selec_pt
	// chooses, unless that move loses at once or by force. Self-play games
	// (the engine on both sides) produce the threats that make this matter.
	int turns = 0, overrides = 0;
	Player d = engine_player(false);
	for (const Opening& o : make_openings(60, 77))
	{
		Reset(false);
		Owner side = o.first;
		for (int q : o.points) { place(q + 1, side); side = side == Computer ? Human : Computer; }
		while (Status() == InProgress && history.size() < (size_t)N2)
		{
			if (side == Computer)
			{
				int original = selec_pt() - 1, plan = computer_pt() - 1;
				turns++;
				if (plan != original)
				{
					overrides++;
					force_nodes = 0;
					CHECK(loses_at_once(original) || forced_after(original, force_depth));
				}
			}
			int q = choose_for(side, d);
			if (q < 0) break;
			place(q + 1, side);
			side = side == Computer ? Human : Computer;
		}
	}
	CHECK(overrides > 0);
	// With the search switched off, games without captures are the original.
	search_without_captures = false;
	int r, c;
	bool same = true;
	srand(4);
	for (int g = 0; g < 30 && same; g++)
	{
		Reset(false);
		bool compTurn = g % 2;
		while (Status() == InProgress && same)
		{
			if (compTurn) { int expected = selec_pt(); if (!ComputerMove(r, c)) break; same = r * N + c + 1 == expected; }
			else scripted_human_move();
			compTurn = !compTurn;
		}
	}
	search_without_captures = true;
	CHECK(same);
	printf("  %d computer turns, %d where the original choice lost and was replaced\n", turns, overrides);
}

static void test_game_logs()
{
	std::vector<GameLog> logs = read_all_logs(games_dir());
	printf("  %zu distinct game logs replay cleanly\n", logs.size());
	for (const GameLog& g : logs) CHECK(replay(g).empty());
}

static void test_selfplay()
{
	// Mirroring twice restores the exact engine state.
	srand(41);
	for (int trial = 0; trial < 200; trial++)
	{
		random_position();
		int p0[N2], sn0[D], w0[N2], cw0[N2];
		memcpy(p0, p, sizeof p); memcpy(sn0, sn, sizeof sn); memcpy(w0, w, sizeof w); memcpy(cw0, cw, sizeof cw);
		int h0 = captures_h, c0 = captures_c;
		mirror_board();
		mirror_board();
		if (memcmp(p0, p, sizeof p) || memcmp(sn0, sn, sizeof sn) || memcmp(w0, w, sizeof w) || memcmp(cw0, cw, sizeof cw)
			|| h0 != captures_h || c0 != captures_c) { CHECK(false); break; }
	}
	// Identical parameters on both sides score exactly 50%.
	for (bool captures : { true, false })
	{
		Player d = engine_player(captures);
		MatchResult m = play_match(d, d, make_openings(20, 99), captures);
		CHECK(m.aWins == m.bWins);
	}
	// Changing one side's values must not leak into the engine's own values.
	Reset(false);
	Player before = engine_player(false), changed = before;
	changed.line.opp3 = 999;
	put("G7", Computer); put("H7", Human);
	choose_for(Human, changed);
	CHECK(engine_player(false).line.opp3 == before.line.opp3);
}

static int run_tests()
{
	struct { const char* name; void (*fn)(); } tests[] = {
		{ "rebuild matches incremental updates", test_rebuild_matches_incremental },
		{ "capture rules", test_capture_rules },
		{ "capture weights", test_capture_weights },
		{ "one-move safety check", test_safety_check },
		{ "forcing-sequence search", test_search },
		{ "games without captures", test_no_capture_games },
		{ "game logs", test_game_logs },
		{ "self-play", test_selfplay },
	};
	for (auto& t : tests)
	{
		int before = g_fails;
		auto t0 = std::chrono::steady_clock::now();
		printf("%s\n", t.name);
		t.fn();
		printf("  %s (%.1f s)\n", g_fails == before ? "ok" : "FAILED", ms_since(t0) / 1000);
	}
	printf(g_fails ? "\n%d check(s) failed\n" : "\nall tests passed\n", g_fails);
	return g_fails ? 1 : 0;
}
