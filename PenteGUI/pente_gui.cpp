// Pente - graphical front end (Win32 + GDI+).
//
// Click an intersection to place your stone; the computer answers after a short
// pause. Keyboard: N = new game (you first), C = new game (computer first),
// U / Ctrl+Z = undo, H = show the computer's evaluation, M = move numbers,
// S = save the board as text.
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>
#include <commdlg.h>
#include <objidl.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>
namespace Gdiplus { using std::min; using std::max; }
#include <gdiplus.h>
#include "pente_engine.h"

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "comdlg32.lib")

using namespace Gdiplus;
using pente::N;

namespace
{
	const wchar_t* kColLetters = L"ABCDEFGHJKLMN";   // 'I' skipped, as on the text board
	const UINT_PTR kFrameTimer = 1;
	const UINT_PTR kComputerTimer = 2;
	const UINT kComputerDelayMs = 550;
	const ULONGLONG kDropMs = 230;

	enum class Phase { HumanTurn, Thinking, Over };

	enum ButtonId { BtnNewHuman, BtnNewComputer, BtnUndo, BtnHints, BtnNumbers, BtnSave, BtnCount };

	struct Button
	{
		RectF rect;
		ButtonId id;
	};

	HWND g_hwnd;
	float g_scale = 1.0f;
	Phase g_phase = Phase::HumanTurn;
	bool g_showHints = false;
	bool g_showNumbers = false;

	// layout
	int g_width, g_height;
	RectF g_boardRect;           // wooden board
	PointF g_gridOrigin;         // screen position of A13 (top-left intersection)
	float g_cell;
	RectF g_panelRect;
	Button g_buttons[BtnCount];

	// interaction / animation
	int g_hoverRow = -1, g_hoverCol = -1;
	int g_hoverButton = -1;
	bool g_tracking = false;
	ULONGLONG g_dropStart = 0;
	ULONGLONG g_overStart = 0;

	Bitmap* g_backBuffer = nullptr;
	Bitmap* g_woodCache = nullptr;

	float S(float v) { return v * g_scale; }

	PointF PointToScreen(int row, int col)
	{
		return PointF(g_gridOrigin.X + col * g_cell, g_gridOrigin.Y + (N - 1 - row) * g_cell);
	}

	std::wstring PointName(int row, int col)
	{
		return std::wstring(1, kColLetters[col]) + std::to_wstring(row + 1);
	}

	Color WithAlpha(const Color& c, float a)
	{
		int alpha = (int)(c.GetA() * std::clamp(a, 0.0f, 1.0f));
		return Color((BYTE)alpha, c.GetR(), c.GetG(), c.GetB());
	}

	Color Lerp(const Color& a, const Color& b, float t)
	{
		auto mix = [t](BYTE x, BYTE y) { return (BYTE)(x + (y - x) * t); };
		return Color(mix(a.GetA(), b.GetA()), mix(a.GetR(), b.GetR()), mix(a.GetG(), b.GetG()), mix(a.GetB(), b.GetB()));
	}

	void AddRoundRect(GraphicsPath& path, const RectF& r, float radius)
	{
		float d = radius * 2;
		path.AddArc(r.X, r.Y, d, d, 180, 90);
		path.AddArc(r.X + r.Width - d, r.Y, d, d, 270, 90);
		path.AddArc(r.X + r.Width - d, r.Y + r.Height - d, d, d, 0, 90);
		path.AddArc(r.X, r.Y + r.Height - d, d, d, 90, 90);
		path.CloseFigure();
	}

	void FillRoundRect(Graphics& g, const RectF& r, float radius, const Brush& brush)
	{
		GraphicsPath path;
		AddRoundRect(path, r, radius);
		g.FillPath(&brush, &path);
	}

	void DrawText(Graphics& g, const std::wstring& text, const RectF& r, float size, int style,
		const Color& color, StringAlignment h = StringAlignmentNear, StringAlignment v = StringAlignmentCenter,
		const wchar_t* family = L"Segoe UI")
	{
		FontFamily ff(family);
		Font font(&ff, size, style, UnitPixel);
		StringFormat fmt;
		fmt.SetAlignment(h);
		fmt.SetLineAlignment(v);
		fmt.SetTrimming(StringTrimmingEllipsisCharacter);
		fmt.SetFormatFlags(StringFormatFlagsNoWrap);
		SolidBrush brush(color);
		g.DrawString(text.c_str(), -1, &font, r, &fmt, &brush);
	}

	// ---------------------------------------------------------------- layout

	void Layout()
	{
		RECT rc;
		GetClientRect(g_hwnd, &rc);
		g_width = std::max<int>(1, rc.right);
		g_height = std::max<int>(1, rc.bottom);

		float panelW = S(320);
		float pad = S(24);
		float areaW = g_width - panelW;
		float size = std::max(S(200), std::min(areaW - 2 * pad, g_height - 2 * pad));
		g_boardRect = RectF((areaW - size) / 2, (g_height - size) / 2, size, size);

		float inset = size * 0.075f;
		g_cell = (size - 2 * inset) / (N - 1);
		g_gridOrigin = PointF(g_boardRect.X + inset, g_boardRect.Y + inset);

		g_panelRect = RectF(areaW, pad, panelW - pad, g_height - 2 * pad);

		float bh = S(38), gap = S(8);
		float y = g_panelRect.GetBottom() - BtnCount * (bh + gap) + gap;
		for (int i = 0; i < BtnCount; i++)
		{
			g_buttons[i] = { RectF(g_panelRect.X, y, g_panelRect.Width, bh), (ButtonId)i };
			y += bh + gap;
		}

		delete g_backBuffer;
		g_backBuffer = new Bitmap(g_width, g_height, PixelFormat32bppPARGB);
		delete g_woodCache;
		g_woodCache = nullptr;
	}

	// ---------------------------------------------------------------- board

	Bitmap* BuildWood()
	{
		int w = (int)std::ceil(g_boardRect.Width), h = (int)std::ceil(g_boardRect.Height);
		Bitmap* bmp = new Bitmap(w + 1, h + 1, PixelFormat32bppPARGB);
		Graphics g(bmp);
		g.SetSmoothingMode(SmoothingModeAntiAlias);
		g.SetTextRenderingHint(TextRenderingHintAntiAlias);
		g.TranslateTransform(-g_boardRect.X, -g_boardRect.Y);

		RectF r = g_boardRect;
		GraphicsPath board;
		AddRoundRect(board, r, S(14));
		LinearGradientBrush wood(PointF(r.X, r.Y), PointF(r.GetRight(), r.GetBottom()),
			Color(255, 228, 188, 128), Color(255, 190, 138, 80));
		g.FillPath(&wood, &board);

		// wood grain: gentle sine waves with a fixed seed so the board never changes
		g.SetClip(&board);
		unsigned seed = 1234567;
		auto rnd = [&seed]() { seed = seed * 1103515245u + 12345u; return ((seed >> 16) & 0x7fff) / 32767.0f; };
		for (int i = 0; i < 90; i++)
		{
			float y0 = r.Y + rnd() * r.Height;
			float amp = S(2) + rnd() * S(6);
			float freq = 0.004f + rnd() * 0.01f;
			float phase = rnd() * 6.28f;
			bool dark = rnd() < 0.6f;
			Pen pen(dark ? Color((BYTE)(18 + rnd() * 26), 110, 60, 20) : Color((BYTE)(20 + rnd() * 20), 255, 235, 200),
				S(0.6f) + rnd() * S(1.8f));
			std::vector<PointF> pts;
			for (float x = r.X - 10; x <= r.GetRight() + 10; x += S(6))
				pts.emplace_back(x, y0 + amp * std::sin(x * freq / g_scale + phase) + S(3) * std::sin(x * 0.031f / g_scale + i));
			g.DrawCurve(&pen, pts.data(), (INT)pts.size());
		}
		g.ResetClip();

		Pen rim(Color(160, 90, 50, 15), S(2));
		g.DrawPath(&rim, &board);

		// grid
		Pen line(Color(200, 70, 40, 15), S(1.2f));
		float x0 = g_gridOrigin.X, y0 = g_gridOrigin.Y, len = g_cell * (N - 1);
		for (int i = 0; i < N; i++)
		{
			g.DrawLine(&line, x0 + i * g_cell, y0, x0 + i * g_cell, y0 + len);
			g.DrawLine(&line, x0, y0 + i * g_cell, x0 + len, y0 + i * g_cell);
		}
		Pen frame(Color(220, 70, 40, 15), S(2.2f));
		g.DrawRectangle(&frame, x0, y0, len, len);

		// star points
		SolidBrush star(Color(230, 70, 40, 15));
		int stars[][2] = { {3,3}, {3,9}, {9,3}, {9,9}, {6,6} };
		for (auto& s : stars)
		{
			PointF p = PointToScreen(s[0], s[1]);
			float d = S(4.5f);
			g.FillEllipse(&star, p.X - d, p.Y - d, 2 * d, 2 * d);
		}

		// coordinates
		float label = std::max(S(10), g_cell * 0.3f);
		float margin = g_gridOrigin.X - g_boardRect.X;
		Color ink(200, 80, 45, 15);
		for (int i = 0; i < N; i++)
		{
			PointF top = PointToScreen(N - 1, i), bottom = PointToScreen(0, i);
			std::wstring letter(1, kColLetters[i]);
			DrawText(g, letter, RectF(top.X - g_cell / 2, g_boardRect.Y, g_cell, margin * 0.85f), label, FontStyleBold, ink, StringAlignmentCenter);
			DrawText(g, letter, RectF(bottom.X - g_cell / 2, bottom.Y + margin * 0.15f, g_cell, margin * 0.85f), label, FontStyleBold, ink, StringAlignmentCenter);
			PointF left = PointToScreen(i, 0), right = PointToScreen(i, N - 1);
			std::wstring num = std::to_wstring(i + 1);
			DrawText(g, num, RectF(g_boardRect.X, left.Y - g_cell / 2, margin * 0.85f, g_cell), label, FontStyleBold, ink, StringAlignmentCenter);
			DrawText(g, num, RectF(right.X + margin * 0.15f, right.Y - g_cell / 2, margin * 0.85f, g_cell), label, FontStyleBold, ink, StringAlignmentCenter);
		}
		return bmp;
	}

	// A glossy stone: drop shadow, radial body, specular highlight.
	void DrawStone(Graphics& g, PointF c, float r, pente::Owner who, float alpha)
	{
		bool human = who == pente::Human;
		SolidBrush shadow(Color((BYTE)(85 * alpha), 30, 15, 0));
		g.FillEllipse(&shadow, c.X - r + r * 0.12f, c.Y - r + r * 0.2f, 2 * r, 2 * r);

		GraphicsPath body;
		body.AddEllipse(c.X - r, c.Y - r, 2 * r, 2 * r);
		PathGradientBrush grad(&body);
		grad.SetCenterPoint(PointF(c.X - r * 0.35f, c.Y - r * 0.4f));
		Color centre = human ? Color(255, 255, 255, 252) : Color(255, 105, 108, 120);
		Color edge = human ? Color(255, 196, 190, 178) : Color(255, 12, 12, 16);
		grad.SetCenterColor(WithAlpha(centre, alpha));
		Color surround = WithAlpha(edge, alpha);
		INT count = 1;
		grad.SetSurroundColors(&surround, &count);
		g.FillPath(&grad, &body);

		SolidBrush gloss(Color((BYTE)((human ? 150 : 90) * alpha), 255, 255, 255));
		g.FillEllipse(&gloss, c.X - r * 0.55f, c.Y - r * 0.62f, r * 0.5f, r * 0.32f);
	}

	Color HeatColor(float v)
	{
		Color cold(255, 70, 130, 255), warm(255, 255, 214, 60), hot(255, 240, 50, 40);
		return v < 0.5f ? Lerp(cold, warm, v * 2) : Lerp(warm, hot, (v - 0.5f) * 2);
	}

	void DrawHints(Graphics& g)
	{
		int minW = INT_MAX, maxW = 0, bestRow = -1, bestCol = -1;
		for (int r = 0; r < N; r++)
			for (int c = 0; c < N; c++)
			{
				int w = pente::Weight(r, c);
				if (pente::Cell(r, c) != pente::Empty || w <= 0) continue;
				minW = std::min(minW, w);
				if (w > maxW) { maxW = w; bestRow = r; bestCol = c; }
			}
		if (maxW == 0) return;
		float span = std::log(1.0f + maxW - minW);
		for (int r = 0; r < N; r++)
			for (int c = 0; c < N; c++)
			{
				int w = pente::Weight(r, c);
				if (pente::Cell(r, c) != pente::Empty || w <= 0) continue;
				float v = span > 0 ? std::log(1.0f + w - minW) / span : 1.0f;
				float rad = g_cell * (0.1f + 0.28f * v);
				PointF p = PointToScreen(r, c);
				SolidBrush b(WithAlpha(HeatColor(v), 0.35f + 0.4f * v));
				g.FillEllipse(&b, p.X - rad, p.Y - rad, 2 * rad, 2 * rad);
			}
		PointF p = PointToScreen(bestRow, bestCol);
		float rad = g_cell * 0.46f;
		Pen ring(Color(230, 200, 20, 20), S(2));
		ring.SetDashStyle(DashStyleDash);
		g.DrawEllipse(&ring, p.X - rad, p.Y - rad, 2 * rad, 2 * rad);
	}

	void DrawBoard(Graphics& g, ULONGLONG now)
	{
		if (!g_woodCache) g_woodCache = BuildWood();

		// board drop shadow
		for (int i = 6; i >= 1; i--)
		{
			SolidBrush sh(Color((BYTE)(14), 0, 0, 0));
			RectF r = g_boardRect;
			r.Inflate(S((float)i * 1.5f), S((float)i * 1.5f));
			r.Offset(0, S(6));
			FillRoundRect(g, r, S(14) + S((float)i), sh);
		}
		g.DrawImage(g_woodCache, g_boardRect.X, g_boardRect.Y);

		if (g_showHints && g_phase != Phase::Over) DrawHints(g);

		const auto& hist = pente::History();
		float r = g_cell * 0.44f;
		for (size_t i = 0; i < hist.size(); i++)
		{
			const pente::Move& m = hist[i];
			if (pente::Cell(m.row, m.col) != m.who) continue;
			PointF c = PointToScreen(m.row, m.col);
			float scale = 1.0f, alpha = 1.0f;
			if (i + 1 == hist.size() && now - g_dropStart < kDropMs)
			{
				float t = (float)(now - g_dropStart) / kDropMs;
				float ease = 1 - (1 - t) * (1 - t) * (1 - t);
				scale = 1.0f + 0.45f * (1 - ease);
				alpha = ease;
			}
			DrawStone(g, c, r * scale, m.who, alpha);

			Color ink = m.who == pente::Human ? Color(255, 60, 50, 40) : Color(255, 235, 230, 220);
			if (g_showNumbers)
				DrawText(g, std::to_wstring(i + 1), RectF(c.X - r, c.Y - r, 2 * r, 2 * r), r * 0.8f, FontStyleBold, WithAlpha(ink, alpha), StringAlignmentCenter);
			else if (i + 1 == hist.size())
			{
				SolidBrush dot(WithAlpha(Color(255, 225, 45, 35), alpha));
				float d = r * 0.22f;
				g.FillEllipse(&dot, c.X - d, c.Y - d, 2 * d, 2 * d);
			}
		}

		// ghost stone under the cursor
		if (g_phase == Phase::HumanTurn && g_hoverRow >= 0 && pente::Cell(g_hoverRow, g_hoverCol) == pente::Empty)
			DrawStone(g, PointToScreen(g_hoverRow, g_hoverCol), r, pente::Human, 0.45f);

		// winning row
		int rows[5], cols[5];
		if (pente::WinningLine(rows, cols))
		{
			float pulse = 0.65f + 0.35f * std::sin((now - g_overStart) / 180.0f);
			PointF a = PointToScreen(rows[0], cols[0]), b = PointToScreen(rows[4], cols[4]);
			Pen glow(Color((BYTE)(90 * pulse), 255, 215, 60), g_cell * 0.55f);
			glow.SetStartCap(LineCapRound);
			glow.SetEndCap(LineCapRound);
			g.DrawLine(&glow, a, b);
			Pen core(Color((BYTE)(230 * pulse), 255, 200, 40), S(4));
			core.SetStartCap(LineCapRound);
			core.SetEndCap(LineCapRound);
			g.DrawLine(&core, a, b);
		}

		// result banner
		if (g_phase == Phase::Over)
		{
			float t = std::min(1.0f, (now - g_overStart) / 400.0f);
			std::wstring msg = pente::Status() == pente::HumanWins ? L"You win!"
				: pente::Status() == pente::ComputerWins ? L"The computer wins" : L"Tie game";
			// keep the banner clear of the winning row
			bool lineInTopHalf = false;
			if (pente::WinningLine(rows, cols)) lineInTopHalf = rows[2] >= N / 2;
			float bh = g_boardRect.Height * 0.12f;
			float by = lineInTopHalf ? g_boardRect.GetBottom() - g_boardRect.Height * 0.06f - bh : g_boardRect.Y + g_boardRect.Height * 0.06f;
			RectF banner(g_boardRect.X + g_boardRect.Width * 0.22f, by, g_boardRect.Width * 0.56f, bh);
			SolidBrush bg(Color((BYTE)(215 * t), 22, 24, 32));
			FillRoundRect(g, banner, S(16), bg);
			RectF top = banner, bottom = banner;
			top.Height *= 0.62f;
			bottom.Y += banner.Height * 0.55f;
			bottom.Height *= 0.4f;
			DrawText(g, msg, top, banner.Height * 0.36f, FontStyleBold, WithAlpha(Color(255, 255, 214, 120), t), StringAlignmentCenter, StringAlignmentFar, L"Georgia");
			DrawText(g, L"Press N or C for a new game", bottom, banner.Height * 0.16f, FontStyleRegular, WithAlpha(Color(255, 200, 200, 210), t), StringAlignmentCenter);
		}
	}

	// ---------------------------------------------------------------- panel

	std::wstring ButtonLabel(ButtonId id)
	{
		switch (id)
		{
		case BtnNewHuman:    return L"New game – you first   (N)";
		case BtnNewComputer: return L"New game – computer first   (C)";
		case BtnUndo:        return L"Undo move   (U)";
		case BtnHints:       return std::wstring(L"Computer's view: ") + (g_showHints ? L"on" : L"off") + L"   (H)";
		case BtnNumbers:     return std::wstring(L"Move numbers: ") + (g_showNumbers ? L"on" : L"off") + L"   (M)";
		case BtnSave:        return L"Save board as text…   (S)";
		default:             return L"";
		}
	}

	RectF Card(Graphics& g, float& y, float height)
	{
		RectF r(g_panelRect.X, y, g_panelRect.Width, height);
		SolidBrush bg(Color(255, 36, 40, 52));
		FillRoundRect(g, r, S(10), bg);
		y += height + S(12);
		r.Inflate(-S(14), -S(10));
		return r;
	}

	void DrawPanel(Graphics& g, ULONGLONG now)
	{
		const Color text(255, 226, 228, 236), dim(255, 140, 146, 162), accent(255, 255, 196, 92);
		float x = g_panelRect.X, w = g_panelRect.Width;
		float y = g_panelRect.Y;

		DrawText(g, L"PENTE", RectF(x, y, w, S(46)), S(40), FontStyleBold, accent, StringAlignmentNear, StringAlignmentCenter, L"Georgia");
		y += S(46);
		DrawText(g, L"Five in a row wins", RectF(x, y, w, S(20)), S(13), FontStyleItalic, dim);
		y += S(32);

		// status
		RectF st = Card(g, y, S(64));
		std::wstring status;
		Color statusColor = text;
		switch (g_phase)
		{
		case Phase::HumanTurn: status = L"Your move"; break;
		case Phase::Thinking:  status = L"Computer is thinking" + std::wstring((now / 300) % 4, L'.'); statusColor = accent; break;
		case Phase::Over:
			status = pente::Status() == pente::HumanWins ? L"You win!" : pente::Status() == pente::ComputerWins ? L"Computer wins" : L"Tie game";
			statusColor = pente::Status() == pente::HumanWins ? Color(255, 120, 220, 140) : Color(255, 255, 120, 110);
			break;
		}
		DrawText(g, status, st, S(22), FontStyleBold, statusColor);

		// players
		RectF pl = Card(g, y, S(84));
		int humanStones = 0, compStones = 0;
		for (const auto& m : pente::History()) (m.who == pente::Human ? humanStones : compStones)++;
		float rowH = pl.Height / 2, r = S(10);
		DrawStone(g, PointF(pl.X + r, pl.Y + rowH / 2), r, pente::Human, 1.0f);
		DrawText(g, L"You  (H)", RectF(pl.X + 3 * r, pl.Y, pl.Width, rowH), S(15), FontStyleRegular, text);
		DrawText(g, std::to_wstring(humanStones) + L" stones", RectF(pl.X, pl.Y, pl.Width, rowH), S(13), FontStyleRegular, dim, StringAlignmentFar);
		DrawStone(g, PointF(pl.X + r, pl.Y + rowH * 1.5f), r, pente::Computer, 1.0f);
		DrawText(g, L"Computer  (X)", RectF(pl.X + 3 * r, pl.Y + rowH, pl.Width, rowH), S(15), FontStyleRegular, text);
		DrawText(g, std::to_wstring(compStones) + L" stones", RectF(pl.X, pl.Y + rowH, pl.Width, rowH), S(13), FontStyleRegular, dim, StringAlignmentFar);

		// stats
		RectF sc = Card(g, y, S(92));
		float lh = sc.Height / 3;
		DrawText(g, L"Open five-lines: " + std::to_wstring(pente::RemainingSolutions()) + L" / 396", RectF(sc.X, sc.Y, sc.Width, lh), S(13), FontStyleRegular, text);
		DrawText(g, L"Capture threats – you: " + std::to_wstring(pente::HumanCaptureThreats()) +
			L"   computer: " + std::to_wstring(pente::ComputerCaptureThreats()), RectF(sc.X, sc.Y + lh, sc.Width, lh), S(13), FontStyleRegular, text);
		std::wstring pointInfo = L"Point: –";
		if (g_hoverRow >= 0)
		{
			pointInfo = L"Point: " + PointName(g_hoverRow, g_hoverCol);
			if (g_showHints && pente::Cell(g_hoverRow, g_hoverCol) == pente::Empty)
				pointInfo += L"   computer weight " + std::to_wstring(pente::Weight(g_hoverRow, g_hoverCol));
		}
		DrawText(g, pointInfo, RectF(sc.X, sc.Y + 2 * lh, sc.Width, lh), S(13), FontStyleRegular, dim);

		// move history (most recent first, as many as fit)
		float histBottom = g_buttons[0].rect.Y - S(12);
		if (histBottom - y > S(60))
		{
			RectF hc = Card(g, y, histBottom - y);
			DrawText(g, L"MOVES", RectF(hc.X, hc.Y, hc.Width, S(20)), S(12), FontStyleBold, dim);
			const auto& hist = pente::History();
			float ly = hc.Y + S(24), lineH = S(21);
			int shown = 0;
			for (int i = (int)hist.size() - 1; i >= 0 && ly + lineH <= hc.GetBottom() + S(4); i--, shown++)
			{
				const auto& m = hist[i];
				float half = hc.Width / 2;
				DrawText(g, std::to_wstring(i + 1) + L".", RectF(hc.X, ly, S(40), lineH), S(13), FontStyleRegular, dim);
				DrawStone(g, PointF(hc.X + S(50), ly + lineH / 2), S(6), m.who, 1.0f);
				DrawText(g, PointName(m.row, m.col), RectF(hc.X + S(64), ly, half, lineH), S(14), shown == 0 ? FontStyleBold : FontStyleRegular, shown == 0 ? accent : text);
				DrawText(g, m.who == pente::Human ? L"you" : L"computer", RectF(hc.X + half, ly, half, lineH), S(12), FontStyleRegular, dim, StringAlignmentFar);
				ly += lineH;
			}
			if (hist.empty())
				DrawText(g, g_phase == Phase::HumanTurn ? L"Click any point on the board" : L"", RectF(hc.X, ly, hc.Width, lineH), S(13), FontStyleItalic, dim);
		}

		// buttons
		for (const Button& b : g_buttons)
		{
			bool hot = g_hoverButton == b.id;
			bool toggledOn = (b.id == BtnHints && g_showHints) || (b.id == BtnNumbers && g_showNumbers);
			Color fill = hot ? Color(255, 70, 78, 100) : toggledOn ? Color(255, 58, 64, 84) : Color(255, 46, 51, 66);
			SolidBrush bg(fill);
			FillRoundRect(g, b.rect, S(8), bg);
			if (toggledOn)
			{
				Pen edge(accent, S(1.5f));
				GraphicsPath path;
				AddRoundRect(path, b.rect, S(8));
				g.DrawPath(&edge, &path);
			}
			RectF tr = b.rect;
			tr.Inflate(-S(14), 0);
			DrawText(g, ButtonLabel(b.id), tr, S(14), FontStyleRegular, hot ? Color(255, 255, 255, 255) : text);
		}
	}

	void Paint(HDC hdc)
	{
		ULONGLONG now = GetTickCount64();
		Graphics g(g_backBuffer);
		g.SetSmoothingMode(SmoothingModeAntiAlias);
		g.SetTextRenderingHint(TextRenderingHintAntiAlias);
		g.SetPixelOffsetMode(PixelOffsetModeHalf);

		LinearGradientBrush bg(PointF(0, 0), PointF(0, (REAL)g_height), Color(255, 30, 34, 44), Color(255, 16, 18, 24));
		g.FillRectangle(&bg, 0, 0, g_width, g_height);

		DrawBoard(g, now);
		DrawPanel(g, now);

		Graphics screen(hdc);
		screen.DrawImage(g_backBuffer, 0, 0);
	}

	// ---------------------------------------------------------------- game flow

	void Redraw() { InvalidateRect(g_hwnd, nullptr, FALSE); }

	void AfterMove()
	{
		g_dropStart = GetTickCount64();
		if (pente::Status() != pente::InProgress)
		{
			g_phase = Phase::Over;
			g_overStart = g_dropStart;
			KillTimer(g_hwnd, kComputerTimer);
		}
		Redraw();
	}

	void ComputerTurn()
	{
		g_phase = Phase::Thinking;
		SetTimer(g_hwnd, kComputerTimer, kComputerDelayMs, nullptr);
		Redraw();
	}

	void NewGame(bool humanFirst)
	{
		KillTimer(g_hwnd, kComputerTimer);
		pente::Reset();
		g_phase = Phase::HumanTurn;
		if (!humanFirst) ComputerTurn();
		Redraw();
	}

	void UndoMove()
	{
		// Takes back the last human move (and the computer's reply, or its pending one).
		if (!pente::Undo()) return;
		KillTimer(g_hwnd, kComputerTimer);
		g_phase = Phase::HumanTurn;
		g_dropStart = 0;
		Redraw();
	}

	void SaveBoard()
	{
		wchar_t file[MAX_PATH] = L"pente_board.txt";
		OPENFILENAMEW ofn = { sizeof(ofn) };
		ofn.hwndOwner = g_hwnd;
		ofn.lpstrFilter = L"Text files (*.txt)\0*.txt\0All files\0*.*\0";
		ofn.lpstrFile = file;
		ofn.nMaxFile = MAX_PATH;
		ofn.lpstrDefExt = L"txt";
		ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
		if (!GetSaveFileNameW(&ofn)) return;

		std::string text = pente::BoardText() + "\nMoves (X = computer, H = human):\n";
		const char* letters = "ABCDEFGHJKLMN";
		const auto& hist = pente::History();
		for (size_t i = 0; i < hist.size(); i++)
			text += std::to_string(i + 1) + ". " + (hist[i].who == pente::Human ? "H " : "X ") + letters[hist[i].col] + std::to_string(hist[i].row + 1) + "\n";

		FILE* f = nullptr;
		if (_wfopen_s(&f, file, L"w") == 0 && f)
		{
			fputs(text.c_str(), f);
			fclose(f);
		}
		else
			MessageBoxW(g_hwnd, L"Could not write the file.", L"Pente", MB_ICONERROR);
	}

	void OnButton(ButtonId id)
	{
		switch (id)
		{
		case BtnNewHuman:    NewGame(true); break;
		case BtnNewComputer: NewGame(false); break;
		case BtnUndo:        UndoMove(); break;
		case BtnHints:       g_showHints = !g_showHints; Redraw(); break;
		case BtnNumbers:     g_showNumbers = !g_showNumbers; Redraw(); break;
		case BtnSave:        SaveBoard(); break;
		default: break;
		}
	}

	void UpdateHover(int mx, int my)
	{
		int row = -1, col = -1;
		float fc = (mx - g_gridOrigin.X) / g_cell, fr = (my - g_gridOrigin.Y) / g_cell;
		int c = (int)std::lround(fc), r = (int)std::lround(fr);
		if (c >= 0 && c < N && r >= 0 && r < N && std::hypot(fc - c, fr - r) < 0.45f)
		{
			col = c;
			row = N - 1 - r;
		}
		int button = -1;
		for (const Button& b : g_buttons)
			if (b.rect.Contains((REAL)mx, (REAL)my)) button = b.id;

		if (row != g_hoverRow || col != g_hoverCol || button != g_hoverButton)
		{
			g_hoverRow = row;
			g_hoverCol = col;
			g_hoverButton = button;
			Redraw();
		}
	}

	bool Animating(ULONGLONG now)
	{
		return now - g_dropStart < kDropMs + 50 || g_phase == Phase::Thinking
			|| (g_phase == Phase::Over && (pente::Status() != pente::Tie || now - g_overStart < 500));
	}

	LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
	{
		switch (msg)
		{
		case WM_CREATE:
			g_hwnd = hwnd;
			Layout();
			SetTimer(hwnd, kFrameTimer, 16, nullptr);
			return 0;

		case WM_SIZE:
			if (wp != SIZE_MINIMIZED) { Layout(); Redraw(); }
			return 0;

		case WM_GETMINMAXINFO:
		{
			auto* mmi = (MINMAXINFO*)lp;
			mmi->ptMinTrackSize.x = (LONG)S(900);
			mmi->ptMinTrackSize.y = (LONG)S(720);
			return 0;
		}

		case WM_ERASEBKGND:
			return 1;

		case WM_PAINT:
		{
			PAINTSTRUCT ps;
			HDC hdc = BeginPaint(hwnd, &ps);
			Paint(hdc);
			EndPaint(hwnd, &ps);
			return 0;
		}

		case WM_TIMER:
			if (wp == kComputerTimer)
			{
				KillTimer(hwnd, kComputerTimer);
				int row, col;
				if (g_phase == Phase::Thinking)
				{
					pente::ComputerMove(row, col);
					g_phase = Phase::HumanTurn;
					AfterMove();
				}
			}
			else if (wp == kFrameTimer && Animating(GetTickCount64()))
				Redraw();
			return 0;

		case WM_MOUSEMOVE:
			if (!g_tracking)
			{
				TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hwnd, 0 };
				g_tracking = TrackMouseEvent(&tme) != FALSE;
			}
			UpdateHover(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
			return 0;

		case WM_MOUSELEAVE:
			g_tracking = false;
			g_hoverRow = g_hoverCol = g_hoverButton = -1;
			Redraw();
			return 0;

		case WM_SETCURSOR:
			if (LOWORD(lp) == HTCLIENT)
			{
				bool clickable = g_hoverButton >= 0 || (g_phase == Phase::HumanTurn && g_hoverRow >= 0 &&
					pente::Cell(g_hoverRow, g_hoverCol) == pente::Empty);
				SetCursor(LoadCursor(nullptr, clickable ? IDC_HAND : IDC_ARROW));
				return TRUE;
			}
			break;

		case WM_LBUTTONDOWN:
			UpdateHover(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
			if (g_hoverButton >= 0)
				OnButton((ButtonId)g_hoverButton);
			else if (g_phase == Phase::HumanTurn && g_hoverRow >= 0 && pente::HumanMove(g_hoverRow, g_hoverCol))
			{
				AfterMove();
				if (g_phase != Phase::Over) ComputerTurn();
			}
			return 0;

		case WM_KEYDOWN:
			switch (wp)
			{
			case 'N': NewGame(true); break;
			case 'C': NewGame(false); break;
			case 'U': UndoMove(); break;
			case 'Z': if (GetKeyState(VK_CONTROL) < 0) UndoMove(); break;
			case 'H': OnButton(BtnHints); break;
			case 'M': OnButton(BtnNumbers); break;
			case 'S': SaveBoard(); break;
			}
			return 0;

		case WM_DESTROY:
			delete g_backBuffer;
			delete g_woodCache;
			g_backBuffer = g_woodCache = nullptr;
			PostQuitMessage(0);
			return 0;
		}
		return DefWindowProcW(hwnd, msg, wp, lp);
	}
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int show)
{
	SetProcessDPIAware();
	HDC screen = GetDC(nullptr);
	g_scale = GetDeviceCaps(screen, LOGPIXELSX) / 96.0f;
	ReleaseDC(nullptr, screen);

	GdiplusStartupInput gsi;
	ULONG_PTR token;
	GdiplusStartup(&token, &gsi, nullptr);

	pente::Reset();

	WNDCLASSEXW wc = { sizeof(wc) };
	wc.lpfnWndProc = WndProc;
	wc.hInstance = inst;
	wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
	wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
	wc.lpszClassName = L"PenteWindow";
	RegisterClassExW(&wc);

	RECT work;
	SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
	int w = std::min((int)S(1200), (int)(work.right - work.left));
	int h = std::min((int)S(860), (int)(work.bottom - work.top));
	HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"Pente", WS_OVERLAPPEDWINDOW,
		work.left + (work.right - work.left - w) / 2, work.top + (work.bottom - work.top - h) / 2, w, h,
		nullptr, nullptr, inst, nullptr);
	ShowWindow(hwnd, show);
	UpdateWindow(hwnd);

	MSG msg;
	while (GetMessageW(&msg, nullptr, 0, 0))
	{
		TranslateMessage(&msg);
		DispatchMessageW(&msg);
	}
	GdiplusShutdown(token);
	return (int)msg.wParam;
}
