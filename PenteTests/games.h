// Reading game logs (the automatic log and the older "Save board as text"
// format), replaying them through the engine, and finding where the computer
// went wrong.
#pragma once
#include <algorithm>
#include <fstream>
#include <regex>
#include <set>
#include <sstream>
#include <windows.h>
#include "util.h"

struct LogEvent
{
	bool undo;
	int number;      // move number as logged
	int point;       // 0-based point
	Owner who;
};

struct GameLog
{
	std::string path;
	std::string name;           // file name only
	bool captureRules = true;
	std::vector<LogEvent> events;
	std::string error;          // set if the file could not be read
};

static GameLog read_log(const std::string& path)
{
	GameLog g;
	g.path = path;
	g.name = path.substr(path.find_last_of("\\/") + 1);
	std::ifstream in(path);
	if (!in) { g.error = "cannot open"; return g; }
	std::string text((std::istreambuf_iterator<char>(in)), {});
	// Automatic logs say "Capture rules: on/off"; manual saves say
	// "Capture rules on." only when they were on.
	g.captureRules = text.find("Capture rules: on") != std::string::npos || text.find("Capture rules on") != std::string::npos;
	std::regex moveRe(R"(^\s*(\d+)\.\s+([HX])\s+([A-HJ-N])(\d+))");
	std::istringstream lines(text);
	std::string line;
	while (std::getline(lines, line))
	{
		std::smatch m;
		if (line.rfind("-- undo", 0) == 0) { g.events.push_back({ true, 0, -1, Empty }); continue; }
		if (!std::regex_search(line, m, moveRe)) continue;
		int q = Q((m[3].str() + m[4].str()).c_str());
		if (q < 0) { g.error = "bad point in: " + line; return g; }
		g.events.push_back({ false, std::stoi(m[1]), q, m[2] == "H" ? Human : Computer });
	}
	if (g.events.empty()) g.error = "no moves found";
	return g;
}

// Replays events into the engine. `onComputerTurn` is called before each
// logged computer move with the logged point; return false to stop there.
// Returns an error message, or "" on success.
template <class F>
static std::string replay(const GameLog& g, F onComputerTurn)
{
	Reset(g.captureRules);
	for (const LogEvent& e : g.events)
	{
		if (e.undo) { if (!Undo()) return "undo with nothing to take back"; continue; }
		if ((int)history.size() + 1 != e.number) return "move " + std::to_string(e.number) + " out of sequence";
		if (p[e.point]) return "move " + std::to_string(e.number) + " (" + nm(e.point) + ") is on an occupied point";
		if (Status() != InProgress) return "move " + std::to_string(e.number) + " after the game ended";
		if (e.who == Computer && !onComputerTurn(e.number, e.point)) return "";
		place(e.point + 1, e.who);
	}
	return "";
}

static std::string replay(const GameLog& g)
{
	return replay(g, [](int, int) { return true; });
}

// Folder holding the logs: "games" next to Pente.sln, found from the program's folder.
static std::string games_dir()
{
	char buf[MAX_PATH];
	DWORD n = GetModuleFileNameA(nullptr, buf, MAX_PATH);
	std::string dir(buf, n);
	while (true)
	{
		size_t slash = dir.find_last_of("\\/");
		if (slash == std::string::npos) break;
		dir.resize(slash);
		if (GetFileAttributesA((dir + "\\Pente.sln").c_str()) != INVALID_FILE_ATTRIBUTES) return dir + "\\games";
	}
	return "games";
}

// All logs in a folder, skipping files whose moves repeat an earlier file's
// (a manual save of a game that was also logged automatically). Automatic logs
// are preferred because they also record undos and the rules.
static std::vector<GameLog> read_all_logs(const std::string& dir, bool quiet = false)
{
	std::vector<std::string> files;
	WIN32_FIND_DATAA fd;
	HANDLE h = FindFirstFileA((dir + "\\*.txt").c_str(), &fd);
	if (h != INVALID_HANDLE_VALUE)
	{
		do files.push_back(dir + "\\" + fd.cFileName); while (FindNextFileA(h, &fd));
		FindClose(h);
	}
	// automatic logs ("2026-...") sort before manual saves ("10-2...")
	std::sort(files.begin(), files.end(), [](const std::string& a, const std::string& b)
	{
		auto isAuto = [](const std::string& f) { return f.substr(f.find_last_of("\\/") + 1).rfind("20", 0) == 0; };
		if (isAuto(a) != isAuto(b)) return isAuto(a);
		return a < b;
	});
	std::vector<GameLog> logs;
	std::set<std::string> seen;
	for (const std::string& f : files)
	{
		GameLog g = read_log(f);
		if (!g.error.empty()) { if (!quiet) printf("  skipping %s: %s\n", g.name.c_str(), g.error.c_str()); continue; }
		// key: the final move sequence after replaying undos
		std::string err = replay(g);
		std::string key = g.captureRules ? "c" : "n";
		for (const auto& m : history) key += (m.who == Human ? "H" : "X") + nm(m.row * N + m.col);
		if (!err.empty()) { if (!quiet) printf("  skipping %s: %s\n", g.name.c_str(), err.c_str()); continue; }
		if (!seen.insert(key).second) { if (!quiet) printf("  (%s repeats an earlier game; skipped)\n", g.name.c_str()); continue; }
		logs.push_back(g);
	}
	return logs;
}

static std::string result_text()
{
	switch (Status())
	{
	case HumanWins:    return WonByCaptures() ? "human wins (five captures)" : "human wins (five in a row)";
	case ComputerWins: return WonByCaptures() ? "computer wins (five captures)" : "computer wins (five in a row)";
	case Tie:          return "tie";
	default:           return "not finished";
	}
}

// Settings for "deep" analysis: a longer forcing search than live play uses.
// Settings for "deep" analysis: longer forcing and quiet-threat searches than
// live play uses.
struct DeepSearch
{
	int depth;
	long budget;
	int savedDepth, savedQuietDepth;
	long savedBudget, savedQuietBudget, savedQuietCall;
	DeepSearch(int d = 5, long b = 400000)
		: depth(d), budget(b), savedDepth(force_depth), savedQuietDepth(quiet_inner_depth),
		  savedBudget(force_budget), savedQuietBudget(quiet_budget), savedQuietCall(quiet_call_budget)
	{
		force_depth = d;
		force_budget = b;
		quiet_inner_depth = 3;
		quiet_budget = 400000;
		quiet_call_budget = 20000;
	}
	~DeepSearch()
	{
		force_depth = savedDepth;
		force_budget = savedBudget;
		quiet_inner_depth = savedQuietDepth;
		quiet_budget = savedQuietBudget;
		quiet_call_budget = savedQuietCall;
	}
};

// After the computer plays q, does the human have a quiet move that wins by force?
static bool quiet_after(int q)
{
	Snapshot s;
	save(s);
	place_stone(q + 1, Computer);
	quiet_nodes = 0;
	bool r = status == InProgress && human_quiet_win();
	restore(s);
	return r;
}

// With the computer to move: does playing q lose by force (deep search,
// including quiet threats)?
static bool deep_loses(int q)
{
	if (loses_at_once(q)) return true;
	force_nodes = 0;
	if (forced_after(q, force_depth)) return true;
	return quiet_after(q);
}

// Best move that does not lose by force (deep search), trying moves in weight
// order; -1 if every move tried loses. Tries at most `limit` moves.
static int deep_safe_move(int limit = 40)
{
	std::vector<int> order;
	for (int i = 0; i < N2; i++) if (!p[i]) order.push_back(i);
	std::stable_sort(order.begin(), order.end(), [](int x, int y) { return total_weight(x) > total_weight(y); });
	for (int i = 0; i < (int)order.size() && i < limit; i++)
		if (!deep_loses(order[i])) return order[i];
	return -1;
}

// Replay a game; at every computer turn compare the logged move with what the
// current engine plays, and (for games the human won) find the first logged
// computer move that loses by force although a holding move existed.
static void analyze_game(const GameLog& g, bool verbose)
{
	printf("=== %s (capture rules %s)\n", g.name.c_str(), g.captureRules ? "on" : "off");
	int differ = 0;
	std::string err = replay(g, [&](int num, int played)
	{
		int pr, pc;
		PlannedMove(pr, pc);
		int planned = pr * N + pc;
		if (planned != played)
		{
			differ++;
			if (verbose)
				printf("  move %2d: logged X %-3s (weight %d); current engine plays %s (weight %d)\n",
					num, nm(played).c_str(), total_weight(played), nm(planned).c_str(), total_weight(planned));
		}
		return true;
	});
	if (!err.empty()) { printf("  replay error: %s\n", err.c_str()); return; }
	bool humanWon = Status() == HumanWins;
	printf("  %zu moves, %s; pairs H %d X %d; computer moves that differ from the current engine: %d\n",
		history.size(), result_text().c_str(), Captures(Human), Captures(Computer), differ);
	if (!humanWon) return;

	// Find the decisive mistake with a deeper search than live play uses.
	DeepSearch deep;
	bool found = false;
	replay(g, [&](int num, int played)
	{
		if (!deep_loses(played)) return true;
		int alt = deep_safe_move();
		if (alt < 0)
		{
			if (!found) printf("  move %2d: X %s loses by force, but so does every move tried: the game was already lost\n", num, nm(played).c_str());
			found = true;
			return false;
		}
		// the move the game plays: live search settings, not the deep ones
		force_depth = deep.savedDepth;
		force_budget = deep.savedBudget;
		plan_valid = false;
		int pr, pc;
		PlannedMove(pr, pc);
		int planned = pr * N + pc;
		force_depth = deep.depth;
		force_budget = deep.budget;
		bool engineOk = !deep_loses(planned);
		printf("  move %2d: X %s loses by force; %s holds. The current engine plays %s, which %s\n",
			num, nm(played).c_str(), nm(alt).c_str(), nm(planned).c_str(), engineOk ? "holds" : "also loses");
		found = true;
		return false;
	});
	if (!found) printf("  no forced loss found by the deep search: the human won with threats the search does not follow\n");
}
