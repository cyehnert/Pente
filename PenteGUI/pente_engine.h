// Pente engine API.
//
// The computer's strategy is the original "solution quint" weighting algorithm
// from Pente_main.cpp; this header exposes it without console I/O so a
// graphical front end can drive it.
//
// Coordinates: row 0 is the bottom row ("Row 1" on the text board) and
// col 0 is the left column ("A").
#pragma once
#include <string>
#include <vector>

namespace pente
{
	const int N = 13;
	const int CapturesToWin = 5;   // captured pairs needed to win when capture rules are on

	enum Owner { Empty = 0, Human = -1, Computer = 1 };
	enum Result { InProgress = 0, ComputerWins, HumanWins, Tie };

	struct Point
	{
		int row;
		int col;
	};

	struct Move
	{
		int row;
		int col;
		Owner who;
		std::vector<Point> captured;   // opponent stones removed by this move
	};

	// Start a new game. With capture rules, a move that brackets exactly two
	// opponent stones in a line (X O O X) removes them, and capturing
	// CapturesToWin pairs wins the game.
	void Reset(bool captureRules);
	bool CaptureRules();

	// Place a human stone. Returns false if the point is occupied or the game is over.
	bool HumanMove(int row, int col);

	// Let the computer choose (see PlannedMove) and place a stone. Returns false
	// if the game is over.
	bool ComputerMove(int& row, int& col);

	// Take back the most recent human move together with any computer replies.
	// Returns false if there is no human move to take back.
	bool Undo();

	Owner Cell(int row, int col);
	Result Status();
	const std::vector<Move>& History();

	// Fills the five points of the winning row. Returns false if nobody has won
	// by five in a row.
	bool WinningLine(int rows[5], int cols[5]);
	bool WonByCaptures();
	int Captures(Owner who);       // pairs captured so far by this player

	// The computer's evaluation of an empty point (0 for occupied points): the
	// line weight, plus the capture weight when capture rules are on. The
	// computer plays the point with the highest Weight.
	int Weight(int row, int col);
	int LineWeight(int row, int col);     // the original five-in-a-row weight
	int CaptureWeight(int row, int col);  // capture bonus/penalty (0 without capture rules)

	// The point the computer will play next: the highest-weighted point that
	// does not let the human win at once or by a short forced sequence;
	// RejectedMoves lists the higher-weighted points skipped for that reason.
	bool PlannedMove(int& row, int& col);
	std::vector<Point> RejectedMoves();

	int RemainingSolutions();      // five-in-a-row lines still open to someone
	int ComputerCaptureThreats();  // capture opportunities currently open to the computer
	int HumanCaptureThreats();     // capture opportunities currently open to the human

	// The board in the original text format ('X' = computer, 'H' = human).
	std::string BoardText();
}
