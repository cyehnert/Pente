// Self-play: the engine playing both sides, each side with its own values.
// The engine always plays "Computer", so for the human side the board is
// mirrored (colours and capture counts swapped, line values and weights
// rebuilt), the move chosen, and the board mirrored back.
#pragma once
#include <set>
#include "util.h"

// The values one side plays with: capture weights, and the line weights for
// the rules of the game being played.
struct Player
{
	CaptureParams cap;
	LineParams line;
};

// The values the engine is currently set to play with, for the given rules.
static Player engine_player(bool captures)
{
	return { params, captures ? line_capture : line_plain };
}

// Switch the engine to `pl` and recompute everything that depends on it.
static void apply_player(const Player& pl)
{
	params = pl.cap;
	(capture_rules ? line_capture : line_plain) = pl.line;
	rebuild();
	update_capture_weights();
	plan_valid = false;
}

static void mirror_board()
{
	for (int i = 0; i < N2; i++) p[i] = -p[i];
	std::swap(captures_h, captures_c);
	rebuild();
	update_capture_weights();
	plan_valid = false;
}

// The move `side` would play with values `pl` (0-based point, -1 if none).
// The engine's own values are restored afterwards.
static int choose_for(Owner side, const Player& pl)
{
	Player saved = engine_player(capture_rules);
	apply_player(pl);
	if (side == Human) mirror_board();
	int q = computer_pt() - 1;
	if (side == Human) mirror_board();
	apply_player(saved);
	return q;
}

struct Opening
{
	std::vector<int> points;   // alternate sides, starting with `first`
	Owner first;
};

// Distinct short openings near the centre: 2-4 stones, alternating sides.
static std::vector<Opening> make_openings(int count, unsigned seed)
{
	std::vector<Opening> out;
	std::set<std::vector<int>> seen;
	srand(seed);
	while ((int)out.size() < count)
	{
		Opening o;
		o.first = (out.size() % 2) ? Human : Computer;
		int stones = 2 + rand() % 3;
		std::vector<int> pts;
		while ((int)pts.size() < stones)
		{
			int q = (4 + rand() % 5) * N + 4 + rand() % 5;
			if (std::find(pts.begin(), pts.end(), q) == pts.end()) pts.push_back(q);
		}
		o.points = pts;
		if (seen.insert(pts).second) out.push_back(o);
	}
	return out;
}

struct GameResult
{
	Result result;
	bool byCaptures;
	int moves;
};

// One game: X (the "Computer" side) plays with px, H with ph.
static GameResult play_game(const Opening& o, const Player& px, const Player& ph, bool captures)
{
	Reset(captures);
	Owner side = o.first;
	for (int q : o.points)
	{
		place(q + 1, side);
		side = side == Computer ? Human : Computer;
	}
	while (status == InProgress && history.size() < (size_t)N2)
	{
		int q = choose_for(side, side == Computer ? px : ph);
		if (q < 0) { status = Tie; break; }
		place(q + 1, side);
		side = side == Computer ? Human : Computer;
	}
	return { status, won_by_captures, (int)history.size() };
}

struct MatchResult
{
	int aWins = 0, bWins = 0, ties = 0, aCapWins = 0, bCapWins = 0;
	int games() const { return aWins + bWins + ties; }
	double score() const { return games() ? (aWins + 0.5 * ties) / games() : 0.5; }
};

// Each opening is played twice, A taking each side once.
static MatchResult play_match(const Player& a, const Player& b, const std::vector<Opening>& openings, bool captures)
{
	MatchResult m;
	for (const Opening& o : openings)
	{
		for (int aIsX = 0; aIsX <= 1; aIsX++)
		{
			GameResult r = aIsX ? play_game(o, a, b, captures) : play_game(o, b, a, captures);
			bool xWon = r.result == ComputerWins, hWon = r.result == HumanWins;
			bool aWon = aIsX ? xWon : hWon, bWon = aIsX ? hWon : xWon;
			if (aWon) { m.aWins++; if (r.byCaptures) m.aCapWins++; }
			else if (bWon) { m.bWins++; if (r.byCaptures) m.bCapWins++; }
			else m.ties++;
		}
	}
	return m;
}
