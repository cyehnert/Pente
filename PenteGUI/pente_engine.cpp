// Pente engine: the solution/weight algorithm from Pente_main.cpp with the
// console menu removed. The evaluation routines (init_*, eval_s, eval_w,
// updat_w, selec_pt, updat_cs, cp_check, num_cs) are unchanged apart from
// living in an unnamed namespace, so the computer plays exactly as before.
//
// Optional capture rules: removing captured stones can reopen five-in-a-row
// lines, which the incremental updates cannot express, so after a capture the
// line values, weights and capture arrays are rebuilt from the board using the
// same scoring. The weights do not yet account for capture opportunities.
#include "pente_engine.h"

namespace
{
	using pente::N;
	const int D = 396;      // 2*(N-4)*(2*N-4)  (solution quints)
	const int N2 = 169;     // N*N
	const int E = 460;      // capture quads

	int p[N2];              // 0 = unused, -1=player, 1 = computer
	int a[D][5];            // point numbers in each solution array
	int t[N2][21];          // solution numbers associated with each point
	int sn[D];              // value of each solution array, 0 = unused, +10=void
	int w[N2];              // weight of each point
	int cp[E][4];           // point numbers in each capture array
	int ct[N2][17];         // capture array numbers associated with each point
	int cs[E];              // each capture solution :
	                        // 0 = no capture, 1 = computer capture, -1 = human capture
	int ncapt_h;
	int ncapt_c;

	bool capture_rules;
	int captures_h;         // pairs captured by the human
	int captures_c;         // pairs captured by the computer
	bool won_by_captures;

	pente::Result status;
	int win_sol;            // index into a[] of the winning quint, or -1
	std::vector<pente::Move> history;

	void updat_w(int);

	void init_a()
	{
		int ip, i, j, k;
		ip = 0;
		//  horizontal arrays with N-4 arrays in each row
		for (j=0; j<N; j++)
			for (i=0; i<N-4; i++)
			{
				for (k=0;k<5;k++) a[ip][k] = j*N + i + k + 1;
				ip++;
			}
		// veritcal arrays with N-4 arrays in each column
		for (j=0; j<N-4; j++)
			for (i=0;i<N; i++)
			{
				for (k=0;k<5;k++) a[ip][k] = j*N + i + k*N + 1;
				ip++;
			}
		// arrays with positive slope
		for (i=0;i<N-4;i++)
			for (j=0; j<N-4-i;j++)
			{
				for (k=0;k<5;k++) a[ip][k] = (j+k)*(N+1) + i + 1;
				ip++;
			}
		for (i=1; i<N-4; i++)
			for (j=i; j<N-4; j++)
			{
				for (k=0; k<5; k++) a[ip][k] = (j+k)*(N+1) - i + 1;
				ip++;
			}
		//  arrays with negative slope
		for (i=4;i<N;i++)
			for (j=0; j<i-3;j++)
			{
				for (k=0;k<5;k++) a[ip][k] = (j+k)*(N-1) + i + 1;
				ip++;
			}
		for (i=1; i<N-4; i++)
			for (j=i+1; j<N-3; j++)
			{
				for (k=0; k<5; k++) a[ip][k] = (j+k)*(N-1) + i + 1;
				ip++;
			}
	}

	void init_t()
	{
		// The array t associates with each point pt (1 to N2)
		// a list of associated solutions
		// Element 0 in the list is the number of solutions
		// Elements 1 to 20 are solution numbers (1 to D)
		int i, j, k, m;
		for (i=0;i<D;i++)
			for (j=0; j<5; j++)
			{
				k= a[i][j];
				t[k-1][0]++;
				m = t[k-1][0];
				t[k-1][m] = i+1;
			}
	}

	void init_w()
	{
		for (int i=0;i<N2;i++) w[i] = t[i][0] * 5;
	}

	void init_cp()
	{
		int ip, i, j, k;
		ip = 0;
		//  horizontal arrays with N-3 arrays in each row
		for (j=0; j<N; j++)
			for (i=0; i<N-3; i++)
			{
				for (k=0;k<4;k++) cp[ip][k] = j*N + i + k + 1;
				ip++;
			}
		// veritcal arrays with N-3 arrays in each column
		for (j=0; j<N-3; j++)
			for (i=0;i<N; i++)
			{
				for (k=0;k<4;k++) cp[ip][k] = j*N + i + k*N + 1;
				ip++;
			}
		// arrays with positive slope
		for (i=0;i<N-3;i++)
			for (j=0; j<N-3-i;j++)
			{
				for (k=0;k<4;k++) cp[ip][k] = (j+k)*(N+1) + i + 1;
				ip++;
			}
		for (i=1; i<N-3; i++)
			for (j=i; j<N-3; j++)
			{
				for (k=0; k<4; k++) cp[ip][k] = (j+k)*(N+1) - i + 1;
				ip++;
			}
		//  arrays with negative slope
		for (i=3;i<N;i++)
			for (j=0; j<i-2;j++)
			{
				for (k=0;k<4;k++) cp[ip][k] = (j+k)*(N-1) + i + 1;
				ip++;
			}
		for (i=1; i<N-3; i++)
			for (j=i+1; j<N-2; j++)
			{
				for (k=0; k<4; k++) cp[ip][k] = (j+k)*(N-1) + i + 1;
				ip++;
			}
	}

	void init_ct()
	{
		// The array ct associates with each point pt (1 to N2)
		// a list of associated capture arrays
		// Element 0 in the list is the number of capture arrays
		// Elements 1 to 16 are capture array numbers (1 to E)
		int i, j, k, m;
		for (i=0;i<E;i++)
			for (j=0; j<4; j++)
			{
				k= cp[i][j];
				ct[k-1][0]++;
				m = ct[k-1][0];
				ct[k-1][m] = i+1;
			}
	}

	void eval_s(int pt, int mplayer)
	{
		int i,k,m,n;
		m = pt - 1;
		n = t[m][0];
		for(i=1;i<=n;i++)
		{
			k = t[m][i];
			if (sn[k-1] > 5) continue;
			if (mplayer * sn[k-1] < 0) sn[k-1] = 10;
			else sn[k-1] += mplayer;
		}
	}

	void eval_w(int pt)
	{
		int xt, yt;
		int i, j, np, k;
		int xmax, xmin, ymax, ymin;
		xt = ((pt-1)%N) + 1;
		yt = (pt-1)/N + 1;
		xmax = xt+4;
		xmin = xt - 4;
		ymax = yt + 4;
		ymin = yt - 4;
		if (xmax > N) xmax = N;
		if (ymax > N) ymax = N;
		if (xmin < 1) xmin = 1;
		if (ymin < 1) ymin = 1;
		// horizontal right
		for (i=xt+1;i<= xmax; i++)
		{
			np = (yt-1)*N + i;
			if (w[np-1]) updat_w(np);
		}
		// horizontal left
		for (i=xt-1;i>=xmin;i--)
		{
			np = (yt-1)*N+i;
			if (w[np-1]) updat_w(np);
		}
		// vertical up
		for (j=yt+1;j<=ymax;j++)
		{
			np = (j-1)*N+xt;
			if (w[np-1]) updat_w(np);
		}
		// vertical down
		for (j=yt-1;j>=ymin;j--)
		{
			np = (j-1)*N + xt;
			if (w[np-1]) updat_w(np);
		}
		// right and up diagonal
		for (k=1;k<=4;k++)
		{
			i = xt + k;
			j = yt + k;
			if (i <= xmax && j <= ymax)
			{
				np = (j-1)*N+i;
				if (w[np-1]) updat_w(np);
			}
		}
		// right and down diagonal
		for (k=1;k<=4;k++)
		{
			i = xt + k;
			j = yt - k;
			if (i <= xmax && j >= ymin)
			{
				np = (j-1)*N + i;
				if (w[np-1]) updat_w(np);
			}
		}
		// left and up diagonal
		for (k=1;k<=4;k++)
		{
			i = xt - k;
			j = yt + k;
			if (i>=xmin && j <= ymax)
			{
				np = (j-1)*N + i;
				if (w[np-1]) updat_w(np);
			}
		}
		// left and down diagonal
		for (k=1;k<=4;k++)
		{
			i = xt - k;
			j = yt - k;
			if (i >= xmin && j >= ymin)
			{
				np = (j-1)*N + i;
				if (w[np-1]) updat_w(np);
			}
		}
	}

	void updat_w(int np)
	{
		int ns, k, ks, ws, n3n, n3p;
		ns = t[np-1][0];
		n3n = 0;
		n3p = 0;
		w[np-1] = 0;
		for (k=1; k<=ns; k++)
		{
			ks = t[np-1][k];
			if (sn[ks-1] == -3) n3n++;
			if (sn[ks-1] == 3) n3p++;
			ws = 5 + sn[ks-1];
			switch( ws )
			{
			case 1: w[np-1] += 1500;   break;  // opponent with 4
			case 2: w[np-1] += 50;     break;  // opponent with 3
			case 3: w[np-1] += 26;     break;  // opponent with 2
			case 4: w[np-1] += 4;      break;  // opponent with 1
			case 5: w[np-1] += 5;      break;  // vacant solution array
			case 6: w[np-1] += 6;      break;  // computer with 1
			case 7: w[np-1] += 29;     break;  // computer with 2
			case 8: w[np-1] += 45;     break;  // computer with 3
			case 9: w[np-1] += 100001; break;  // computer with 4
			default: break;
			}
		}
		if (n3n > 1) w[np-1] += 50;
		if (n3p > 1) w[np-1] += 1000;
	}

	// Returns 0 when no vacant point carries any weight (the original read an
	// uninitialized index in that case).
	int selec_pt()
	{
		int i, maxw, j, maxi;
		maxw = 0;
		maxi = -1;
		for (i=0; i< N2; i++)
		{
			j = w[i];
			if (j > maxw)
			{
				maxi = i;
				maxw = j;
			}
		}
		return maxi+1;
	}

	// Same tests as the original check_win, but reports the outcome instead of printing it.
	pente::Result check_win()
	{
		int i;
		bool tie = true;
		for (i=0;i<D;i++)
			if (sn[i] != 10) { tie = false; break; }
		if (tie) return pente::Tie;
		for (i = 0; i<D; i++)
		{
			if (sn[i] == 5)  { win_sol = i; return pente::ComputerWins; }
			if (sn[i] == -5) { win_sol = i; return pente::HumanWins; }
		}
		return pente::InProgress;
	}

	int cp_check(int jct)
	{
		int cc=0;
		int hc=0;
		int p0 = cp[jct-1][0];
		int p1 = cp[jct-1][1];
		int p2 = cp[jct-1][2];
		int p3 = cp[jct-1][3];
		if ((p[p1-1] == p[p2-1]) && p[p1-1] != 0)
		{
			if (p[p0-1] == 0 && (-1)*p[p3-1] == p[p2-1])
			{
				if (p[p1-1] > 0) hc = 1;
				else cc=1;
			}
			else if (p[p3-1] == 0 && (-1)*p[p0-1] == p[p1-1])
			{
				if (p[p1-1] > 0) hc = 1;
				else cc=1;
			}
		}
		if (cc == 1) return 1;
		if (hc == 1) return -1;
		return 0;
	}

	void updat_cs(int pt)
	{
		int njct = ct[pt-1][0];   // number of capture arrays at this point
		for (int i=0;i<njct;i++)
		{
			int jct = ct[pt-1][i+1];
			cs[jct-1] = cp_check(jct);
		}
	}

	void num_cs()
	{
		ncapt_h = 0;
		ncapt_c = 0;
		for (int i=0;i<E;i++)
		{
			if (cs[i] == 1) ncapt_c++;
			else if (cs[i] == (-1)) ncapt_h++;
		}
	}

	// Remove opponent pairs bracketed by the stone just placed at pt (X O O X
	// along any of the 8 directions). Only the moving player captures; placing a
	// stone between two enemy stones is safe.
	void take_captures(int pt, int mplayer, std::vector<pente::Point>& captured)
	{
		static const int dirs[8][2] = { {1,0}, {-1,0}, {0,1}, {0,-1}, {1,1}, {1,-1}, {-1,1}, {-1,-1} };
		int x = (pt-1)%N, y = (pt-1)/N;
		for (const auto& d : dirs)
		{
			int x3 = x + 3*d[0], y3 = y + 3*d[1];
			if (x3 < 0 || x3 >= N || y3 < 0 || y3 >= N) continue;
			int i1 = (y + d[1])*N + x + d[0];
			int i2 = (y + 2*d[1])*N + x + 2*d[0];
			int i3 = y3*N + x3;
			if (p[i1] == -mplayer && p[i2] == -mplayer && p[i3] == mplayer)
			{
				p[i1] = 0;
				p[i2] = 0;
				captured.push_back({ i1/N, i1%N });
				captured.push_back({ i2/N, i2%N });
			}
		}
	}

	// Recompute sn, w and cs from the board; equivalent to having reached the
	// current position through the incremental updates without captures.
	void rebuild()
	{
		for (int i=0;i<D;i++)
		{
			int nh = 0, nc = 0;
			for (int k=0;k<5;k++)
			{
				int v = p[a[i][k]-1];
				if (v > 0) nc++;
				else if (v < 0) nh++;
			}
			sn[i] = (nh && nc) ? 10 : nc - nh;
		}
		for (int i=0;i<N2;i++)
		{
			if (p[i]) w[i] = 0;
			else updat_w(i+1);
		}
		for (int j=0;j<E;j++) cs[j] = cp_check(j+1);
		num_cs();
	}

	// One move, as performed by both branches of the original main loop,
	// plus captures when they are enabled.
	void place(int pt, int mplayer)
	{
		p[pt-1] = mplayer;
		w[pt-1] = 0;
		pente::Move move{ (pt-1)/N, (pt-1)%N, (pente::Owner)mplayer, {} };
		if (capture_rules) take_captures(pt, mplayer, move.captured);

		if (move.captured.empty())
		{
			eval_s(pt, mplayer);
			history.push_back(move);
			status = check_win();
			if (status != pente::InProgress) return;
			eval_w(pt);
			updat_cs(pt);
			num_cs();
			return;
		}

		int& pairs = mplayer == pente::Computer ? captures_c : captures_h;
		pairs += (int)move.captured.size() / 2;
		history.push_back(move);
		rebuild();
		status = check_win();
		if (status != pente::ComputerWins && status != pente::HumanWins && pairs >= pente::CapturesToWin)
		{
			status = mplayer == pente::Computer ? pente::ComputerWins : pente::HumanWins;
			won_by_captures = true;
		}
	}

	void clear_state()
	{
		for (int i=0;i<N2;i++)
		{
			p[i] = 0;
			for (int j=0;j<21;j++) t[i][j] = 0;
			for (int j=0;j<17;j++) ct[i][j] = 0;
		}
		for (int i=0;i<D;i++) sn[i] = 0;
		for (int i=0;i<E;i++) cs[i] = 0;
		ncapt_h = 0;
		ncapt_c = 0;
		captures_h = 0;
		captures_c = 0;
		won_by_captures = false;
		status = pente::InProgress;
		win_sol = -1;
		history.clear();
		init_a();
		init_t();
		init_w();
		init_cp();
		init_ct();
	}

	bool in_range(int row, int col) { return row >= 0 && row < N && col >= 0 && col < N; }
}

namespace pente
{
	void Reset(bool captureRules)
	{
		capture_rules = captureRules;
		clear_state();
	}

	bool CaptureRules() { return capture_rules; }

	bool HumanMove(int row, int col)
	{
		if (status != InProgress || !in_range(row, col)) return false;
		int pt = row*N + col + 1;
		if (p[pt-1]) return false;
		place(pt, Human);
		return true;
	}

	bool ComputerMove(int& row, int& col)
	{
		if (status != InProgress) return false;
		int pt = selec_pt();
		if (pt == 0)
		{
			status = Tie;
			return false;
		}
		place(pt, Computer);
		row = (pt-1)/N;
		col = (pt-1)%N;
		return true;
	}

	bool Undo()
	{
		size_t keep = history.size();
		while (keep > 0 && history[keep-1].who == Computer) keep--;
		if (keep == 0) return false;
		keep--;   // drop the human move too
		std::vector<Move> replay(history.begin(), history.begin() + keep);
		clear_state();
		for (const Move& m : replay) place(m.row*N + m.col + 1, m.who);   // captures replay too
		return true;
	}

	Owner Cell(int row, int col) { return in_range(row, col) ? (Owner)p[row*N + col] : Empty; }
	Result Status() { return status; }
	const std::vector<Move>& History() { return history; }

	bool WinningLine(int rows[5], int cols[5])
	{
		if (win_sol < 0 || won_by_captures || (status != ComputerWins && status != HumanWins)) return false;
		for (int k=0;k<5;k++)
		{
			rows[k] = (a[win_sol][k]-1)/N;
			cols[k] = (a[win_sol][k]-1)%N;
		}
		return true;
	}

	bool WonByCaptures() { return won_by_captures; }
	int Captures(Owner who) { return who == Computer ? captures_c : who == Human ? captures_h : 0; }

	int Weight(int row, int col) { return in_range(row, col) ? w[row*N + col] : 0; }

	int RemainingSolutions()
	{
		int n = 0;
		for (int j=0;j<D;j++) if (sn[j] == 10) n++;
		return D - n;
	}

	int ComputerCaptureThreats() { return ncapt_c; }
	int HumanCaptureThreats() { return ncapt_h; }

	std::string BoardText()
	{
		static const char* cols = "A B C D E F G H J K L M N";
		std::string s = "Row\n";
		for (int j=N;j>0;j--)
		{
			s += (j > 9 ? "" : " ") + std::to_string(j) + "    ";
			for (int k=0;k<N;k++)
			{
				int v = p[(j-1)*N + k];
				s += (v == Human ? 'H' : v == Computer ? 'X' : ' ');
				s += ' ';
			}
			s += "\n\n";
		}
		s += "Col   ";
		s += cols;
		s += "\n";
		return s;
	}
}
