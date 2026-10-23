// Pente engine: the solution/weight algorithm from Pente_main.cpp with the
// console menu removed. The evaluation routines (init_*, eval_s, eval_w,
// updat_w, selec_pt, updat_cs, cp_check, num_cs) are unchanged apart from
// living in an unnamed namespace and taking their values from LineParams
// (still the original values).
//
// Optional capture rules: removing captured stones can reopen five-in-a-row
// lines, which the incremental updates cannot express, so after a capture the
// line values, weights and capture arrays are rebuilt from the board using the
// same scoring.
//
// With capture rules on, the computer also keeps a parallel set of capture
// weights (cw) and plays the empty point with the highest w + cw. The original
// weights w are never changed by this.
//
// In every game the computer checks its candidate moves before playing: it
// skips a move if the human could then win at once (five in a row or a fifth
// capture), or could force a win with a short series of fours and capture-win
// threats, e.g. a four plus an open three. Otherwise it plays the highest-
// weighted point, as the original program did. See plan_move.
#include "pente_engine.h"
#include <algorithm>

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

	// The values updat_w gives each solution array through a point, by how many
	// stones of one side it holds. The original values are kept for games with
	// capture rules; games without captures use a separate set so it can be
	// tuned on its own (PenteTests tune-lines).
	struct LineParams
	{
		int opp4 = 1500;     // opponent with 4
		int opp3 = 50;       // opponent with 3
		int opp2 = 26;       // opponent with 2
		int opp1 = 4;        // opponent with 1
		int vacant = 5;      // vacant solution array
		int own1 = 6;        // computer with 1
		int own2 = 29;       // computer with 2
		int own3 = 45;       // computer with 3
		int own4 = 100001;   // computer with 4
		int opp3_twice = 50;     // point in two opponent threes
		int own3_twice = 1000;   // point in two computer threes
	};
	LineParams line_capture;     // games with capture rules (original values)
	LineParams line_plain;       // games without capture rules

	const LineParams& line_params() { return capture_rules ? line_capture : line_plain; }

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
		for (int i=0;i<N2;i++) w[i] = t[i][0] * line_params().vacant;
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
		const LineParams& lp = line_params();
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
			case 1: w[np-1] += lp.opp4;   break;  // opponent with 4
			case 2: w[np-1] += lp.opp3;   break;  // opponent with 3
			case 3: w[np-1] += lp.opp2;   break;  // opponent with 2
			case 4: w[np-1] += lp.opp1;   break;  // opponent with 1
			case 5: w[np-1] += lp.vacant; break;  // vacant solution array
			case 6: w[np-1] += lp.own1;   break;  // computer with 1
			case 7: w[np-1] += lp.own2;   break;  // computer with 2
			case 8: w[np-1] += lp.own3;   break;  // computer with 3
			case 9: w[np-1] += lp.own4;   break;  // computer with 4
			default: break;
			}
		}
		if (n3n > 1) w[np-1] += lp.opp3_twice;
		if (n3p > 1) w[np-1] += lp.own3_twice;
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

	// ---------------------------------------------------------------
	// Capture weights, from the computer's point of view.

	int cw[N2];                          // capture weight of each point
	bool use_capture_weights = true;     // tests switch this off to compare

	// The capture-weight values. They are variables so the tuner in PenteTests
	// can try other values; these defaults are what the game uses.
	struct CaptureParams
	{
		int win_capture = 100001;   // takes the fifth pair: wins like completing five
		int block_win = 2000;       // stops the human taking their fifth pair
		int capture = 300;          // per pair taken
		int block = 250;            // per own pair saved from capture
		int per_capture = 50;       // added per pair the player already holds
		int threat = 30;            // sets up X O O _ (a capture threat)
		int expose = 300;           // forms own pair open to capture (O X X _)
		int expose_loss = 5000;     // ... when that pair would be the human's fifth
		int break_four = 1500;      // capture breaks a human four (the updat_w value)
		int break_three = 50;       // ... a human three
		int break_two = 26;         // ... a human two
		int keep_three = 100;       // saved pair keeps a computer three
		int keep_two = 30;          // ... a computer two
	};
	CaptureParams params;

	const int OFF_BOARD = 2;

	int stone_at(int x, int y)
	{
		return (x < 0 || x >= N || y < 0 || y >= N) ? OFF_BOARD : p[y*N + x];
	}

	// Strongest line through stone s that belongs only to `owner`, valued on the
	// updat_w scale: what capturing s would break up (or losing it would cost).
	int line_value(int s, int owner)
	{
		int best = 0;
		for (int k=1;k<=t[s][0];k++)
		{
			int v = sn[t[s][k]-1] * owner;   // stones of `owner` in an unblocked line
			if (v < 0 || v > 5) continue;
			int value = owner == pente::Human
				? (v >= 4 ? params.break_four : v == 3 ? params.break_three : v == 2 ? params.break_two : 0)
				: (v >= 3 ? params.keep_three : v == 2 ? params.keep_two : 0);
			if (value > best) best = value;
		}
		return best;
	}

	void capture_weight(int q)
	{
		static const int dirs[8][2] = { {1,0}, {-1,0}, {0,1}, {0,-1}, {1,1}, {1,-1}, {-1,1}, {-1,-1} };
		const int C = pente::Computer, H = pente::Human;
		int x = q%N, y = q/N;
		int take = 0, save = 0, threats = 0, exposed = 0;
		int takeLine = 0, saveLine = 0;
		for (const auto& d : dirs)
		{
			int s1 = stone_at(x + d[0], y + d[1]);
			int s2 = stone_at(x + 2*d[0], y + 2*d[1]);
			int s3 = stone_at(x + 3*d[0], y + 3*d[1]);
			int back = stone_at(x - d[0], y - d[1]);
			if (s1 == H && s2 == H && s3 == C)          // computer captures here
			{
				take++;
				int i1 = (y + d[1])*N + x + d[0], i2 = (y + 2*d[1])*N + x + 2*d[0];
				takeLine = std::max(takeLine, std::max(line_value(i1, H), line_value(i2, H)));
			}
			if (s1 == C && s2 == C && s3 == H)          // human would capture here
			{
				save++;
				int i1 = (y + d[1])*N + x + d[0], i2 = (y + 2*d[1])*N + x + 2*d[0];
				saveLine = std::max(saveLine, std::max(line_value(i1, C), line_value(i2, C)));
			}
			if (s1 == H && s2 == H && s3 == 0)          // threatens a capture next move
				threats++;
			if (s1 == C && ((back == H && s2 == 0) || (back == 0 && s2 == H)))
				exposed++;                              // makes an O X X _ shape
		}

		int value = 0;
		if (take)
		{
			if (captures_c + take >= pente::CapturesToWin) value += params.win_capture;
			else value += take * (params.capture + params.per_capture * captures_c) + takeLine;
		}
		if (save)
		{
			if (captures_h + save >= pente::CapturesToWin) value += params.block_win;
			else value += save * (params.block + params.per_capture * captures_h) + saveLine;
		}
		value += threats * params.threat;
		if (exposed)
		{
			if (captures_h + 1 >= pente::CapturesToWin) value -= params.expose_loss;
			else value -= exposed * (params.expose + params.per_capture * captures_h);
		}
		cw[q] = value;
	}

	void update_capture_weights()
	{
		for (int i=0;i<N2;i++)
		{
			if (capture_rules && use_capture_weights && p[i] == 0) capture_weight(i);
			else cw[i] = 0;
		}
	}

	// The weight the computer chooses by: w alone without capture rules, else
	// w + cw, kept at 1 or more so every empty point stays playable.
	int total_weight(int i)
	{
		if (p[i]) return 0;
		if (!capture_rules || !use_capture_weights) return w[i];
		return std::max(1, w[i] + cw[i]);
	}

	// selec_pt over the combined weights (same tie-break: lowest point wins).
	int choose_pt()
	{
		if (!capture_rules || !use_capture_weights) return selec_pt();
		int maxw = 0, maxi = -1;
		for (int i=0;i<N2;i++)
		{
			int j = total_weight(i);
			if (j > maxw) { maxi = i; maxw = j; }
		}
		return maxi + 1;
	}

	// One move, as performed by both branches of the original main loop,
	// plus captures when they are enabled.
	void place_stone(int pt, int mplayer)
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

	// ---------------------------------------------------------------
	// One-ply safety check (capture rules only).

	bool use_safety_check = true;        // tests switch this off to compare
	bool search_without_captures = true;  // check moves in games without captures too (off: original choice)
	bool plan_valid = false;
	int planned_pt = 0;                  // 1-based point, 0 = no move
	std::vector<int> rejected;           // higher-weighted points skipped as losing

	// Forcing-sequence search limits.
	int force_depth = 3;                 // human forcing moves looked through (tests vary it)
	long force_budget = 20000;           // stones placed per plan (about 0.3 s at most; analysis raises it)
	long force_nodes = 0;
	int forced_checks = 0;               // statistics for tests
	bool budget_hit = false;

	void place(int pt, int mplayer)
	{
		place_stone(pt, mplayer);
		update_capture_weights();
		plan_valid = false;
	}

	// Everything place() can change; a, t, cp and ct are fixed after clear_state.
	struct Snapshot
	{
		int p[N2], sn[D], w[N2], cs[E], cw[N2];
		int ncapt_h, ncapt_c, captures_h, captures_c, win_sol;
		bool won_by_captures;
		pente::Result status;
		size_t moves;
	};

	void save(Snapshot& s)
	{
		std::copy(p, p + N2, s.p);
		std::copy(sn, sn + D, s.sn);
		std::copy(w, w + N2, s.w);
		std::copy(cs, cs + E, s.cs);
		std::copy(cw, cw + N2, s.cw);
		s.ncapt_h = ncapt_h; s.ncapt_c = ncapt_c;
		s.captures_h = captures_h; s.captures_c = captures_c;
		s.win_sol = win_sol; s.won_by_captures = won_by_captures;
		s.status = status;
		s.moves = history.size();
	}

	void restore(const Snapshot& s)
	{
		std::copy(s.p, s.p + N2, p);
		std::copy(s.sn, s.sn + D, sn);
		std::copy(s.w, s.w + N2, w);
		std::copy(s.cs, s.cs + E, cs);
		std::copy(s.cw, s.cw + N2, cw);
		ncapt_h = s.ncapt_h; ncapt_c = s.ncapt_c;
		captures_h = s.captures_h; captures_c = s.captures_c;
		win_sol = s.win_sol; won_by_captures = s.won_by_captures;
		status = s.status;
		history.resize(s.moves);
	}

	// Can the human win with their next stone? A five needs a line holding four
	// human stones and nothing else (sn == -4); a capture win needs enough pairs
	// takeable with one stone.
	bool human_wins_next()
	{
		for (int k=0;k<D;k++)
			if (sn[k] == -4) return true;
		if (!capture_rules) return false;
		static const int dirs[8][2] = { {1,0}, {-1,0}, {0,1}, {0,-1}, {1,1}, {1,-1}, {-1,1}, {-1,-1} };
		for (int q=0;q<N2;q++)
		{
			if (p[q]) continue;
			int x = q%N, y = q/N, pairs = 0;
			for (const auto& d : dirs)
				if (stone_at(x + d[0], y + d[1]) == pente::Computer && stone_at(x + 2*d[0], y + 2*d[1]) == pente::Computer
					&& stone_at(x + 3*d[0], y + 3*d[1]) == pente::Human)
					pairs++;
			if (captures_h + pairs >= pente::CapturesToWin) return true;
		}
		return false;
	}

	// Pairs `mover` would capture by playing the empty point q.
	int pairs_at(int q, int mover)
	{
		static const int dirs[8][2] = { {1,0}, {-1,0}, {0,1}, {0,-1}, {1,1}, {1,-1}, {-1,1}, {-1,-1} };
		int x = q%N, y = q/N, pairs = 0;
		for (const auto& d : dirs)
			if (stone_at(x + d[0], y + d[1]) == -mover && stone_at(x + 2*d[0], y + 2*d[1]) == -mover
				&& stone_at(x + 3*d[0], y + 3*d[1]) == mover)
				pairs++;
		return pairs;
	}

	void add_unique(std::vector<int>& v, int q)
	{
		if (std::find(v.begin(), v.end(), q) == v.end()) v.push_back(q);
	}

	// Empty points where one stone wins for `who`: completes a five (a line
	// holding four of their stones and nothing else) or takes their fifth pair.
	std::vector<int> win_points(int who)
	{
		std::vector<int> pts;
		for (int k=0;k<D;k++)
			if (sn[k] == 4*who)
				for (int j=0;j<5;j++)
					if (!p[a[k][j]-1]) add_unique(pts, a[k][j]-1);
		if (capture_rules)
		{
			int held = who == pente::Human ? captures_h : captures_c;
			for (int q=0;q<N2;q++)
				if (!p[q] && held + pairs_at(q, who) >= pente::CapturesToWin) add_unique(pts, q);
		}
		return pts;
	}

	// Human moves that may create a winning point: making a four (an empty
	// point in a line holding three human stones and nothing else), or, when
	// the human is near five captures, setting up a capture (H X X _).
	std::vector<int> human_forcing_moves()
	{
		std::vector<int> moves;
		for (int k=0;k<D;k++)
			if (sn[k] == -3)
				for (int j=0;j<5;j++)
					if (!p[a[k][j]-1]) add_unique(moves, a[k][j]-1);
		if (capture_rules && captures_h >= pente::CapturesToWin - 2)
		{
			static const int dirs[8][2] = { {1,0}, {-1,0}, {0,1}, {0,-1}, {1,1}, {1,-1}, {-1,1}, {-1,-1} };
			for (int q=0;q<N2;q++)
			{
				if (p[q]) continue;
				int x = q%N, y = q/N;
				for (const auto& d : dirs)
					if (stone_at(x + d[0], y + d[1]) == pente::Computer && stone_at(x + 2*d[0], y + 2*d[1]) == pente::Computer
						&& stone_at(x + 3*d[0], y + 3*d[1]) == 0)
					{
						add_unique(moves, q);
						break;
					}
			}
		}
		return moves;
	}

	// With the human to move, can they force a win within `depth` forcing
	// moves? Each forcing move must leave the human a winning point; the
	// computer may then answer on any winning point of the human's, with any
	// capture, or with its own winning point. Those are its only answers to an
	// immediate threat, so a "true" here is a real forced win. Moves that only
	// threaten (open threes) are not followed, so not every forced win is found.
	bool human_forced_win(int depth)
	{
		if (!win_points(pente::Human).empty()) return true;
		if (depth == 0 || force_nodes > force_budget)
		{
			if (force_nodes > force_budget) budget_hit = true;
			return false;
		}
		Snapshot before;
		save(before);
		for (int h : human_forcing_moves())
		{
			force_nodes++;
			place_stone(h + 1, pente::Human);
			bool forced = false;
			if (status == pente::HumanWins) forced = true;
			else if (status == pente::InProgress)
			{
				std::vector<int> threats = win_points(pente::Human);
				if (!threats.empty())
				{
					std::vector<int> answers = threats;
					for (int c : win_points(pente::Computer)) add_unique(answers, c);
					for (int q=0;q<N2;q++)
						if (!p[q] && pairs_at(q, pente::Computer) > 0) add_unique(answers, q);
					forced = true;
					Snapshot afterThreat;
					save(afterThreat);
					for (int c : answers)
					{
						force_nodes++;
						place_stone(c + 1, pente::Computer);
						bool holds = status == pente::ComputerWins || status == pente::Tie
							|| (status == pente::InProgress && !human_forced_win(depth - 1));
						restore(afterThreat);
						if (holds) { forced = false; break; }
					}
				}
			}
			restore(before);
			if (forced) return true;
		}
		return false;
	}

	// Rank empty points by total weight (ties: lowest point first, as selec_pt)
	// and play the first that wins outright, or after which the human has
	// neither an immediate win nor a forced one. If every move loses at once,
	// play the top-weighted point; if every move loses to a forced sequence,
	// play the top-weighted point that at least survives the next move.
	void plan_move()
	{
		plan_valid = true;
		rejected.clear();
		planned_pt = choose_pt();
		if ((!capture_rules && !search_without_captures) || !use_capture_weights || !use_safety_check || planned_pt == 0) return;

		std::vector<int> order;
		for (int i=0;i<N2;i++) if (!p[i]) order.push_back(i);
		std::stable_sort(order.begin(), order.end(), [](int x, int y) { return total_weight(x) > total_weight(y); });

		force_nodes = 0;
		budget_hit = false;
		Snapshot snap;
		save(snap);
		int firstSurvivor = -1;              // best move that does not lose at once
		size_t rejectedAtOnce = 0;           // rejected[] entries before firstSurvivor
		for (int q : order)
		{
			force_nodes++;
			place_stone(q + 1, pente::Computer);
			bool wins = status == pente::ComputerWins;
			bool survives = wins || status == pente::Tie || (status == pente::InProgress && !human_wins_next());
			bool safe = survives;
			if (survives && !wins && status == pente::InProgress && force_nodes <= force_budget)
			{
				forced_checks++;
				safe = !human_forced_win(force_depth);
			}
			restore(snap);
			if (safe)
			{
				planned_pt = q + 1;
				plan_valid = true;
				return;
			}
			if (survives && firstSurvivor < 0)
			{
				firstSurvivor = q;
				rejectedAtOnce = rejected.size();
			}
			rejected.push_back(q);
		}
		// Everything loses. Prefer a move that survives the next human move.
		if (firstSurvivor >= 0)
		{
			planned_pt = firstSurvivor + 1;
			rejected.resize(rejectedAtOnce);
		}
		else
			rejected.clear();
		plan_valid = true;
	}

	int computer_pt()
	{
		if (!plan_valid) plan_move();
		return planned_pt;
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
		for (int i=0;i<N2;i++) cw[i] = 0;
		plan_valid = false;
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
		int pt = computer_pt();
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

	int Weight(int row, int col) { return in_range(row, col) ? total_weight(row*N + col) : 0; }
	int LineWeight(int row, int col) { return in_range(row, col) && !p[row*N + col] ? w[row*N + col] : 0; }
	int CaptureWeight(int row, int col) { return in_range(row, col) ? cw[row*N + col] : 0; }

	bool PlannedMove(int& row, int& col)
	{
		if (status != InProgress) return false;
		int pt = computer_pt();
		if (pt == 0) return false;
		row = (pt-1)/N;
		col = (pt-1)%N;
		return true;
	}

	std::vector<Point> RejectedMoves()
	{
		std::vector<Point> out;
		if (status != InProgress) return out;
		computer_pt();
		for (int q : rejected) out.push_back({ q/N, q%N });
		return out;
	}

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
