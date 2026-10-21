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

	enum Owner { Empty = 0, Human = -1, Computer = 1 };
	enum Result { InProgress = 0, ComputerWins, HumanWins, Tie };

	struct Move
	{
		int row;
		int col;
		Owner who;
	};

	void Reset();

	// Place a human stone. Returns false if the point is occupied or the game is over.
	bool HumanMove(int row, int col);

	// Let the computer choose and place a stone. Returns false if the game is over.
	bool ComputerMove(int& row, int& col);

	// Take back the most recent human move together with any computer replies.
	// Returns false if there is no human move to take back.
	bool Undo();

	Owner Cell(int row, int col);
	Result Status();
	const std::vector<Move>& History();

	// Fills the five points of the winning row. Returns false if nobody has won.
	bool WinningLine(int rows[5], int cols[5]);

	// The computer's current evaluation of an empty point (0 for occupied points).
	int Weight(int row, int col);

	int RemainingSolutions();      // five-in-a-row lines still open to someone
	int ComputerCaptureThreats();  // capture opportunities currently open to the computer
	int HumanCaptureThreats();     // capture opportunities currently open to the human

	// The board in the original text format ('X' = computer, 'H' = human).
	std::string BoardText();
}
