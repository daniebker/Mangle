#include "nav.h"

static bool overview = true;

void nav_set_overview(bool on)
{
	overview = on;
}

bool nav_overview(void)
{
	return overview;
}

// Panel mode on a page that has panels (whether at the overview or a panel).
static bool paneled(const Book* b, Pos p)
{
	return p.mode == MODE_PANEL && b->pages[p.page].npanels > 0;
}

// First position on a page when arriving from the previous one.
static int first_panel(const Book* b, Pos p)
{
	return (overview && paneled(b, p)) ? PANEL_OVERVIEW : 0;
}

Pos nav_start(const Book* b)
{
	Pos p   = { 0, 0, MODE_PANEL };
	p.panel = first_panel(b, p);
	return p;
}

const char* nav_image(const Book* b, Pos p)
{
	const Page* pg = &b->pages[p.page];
	if (paneled(b, p) && p.panel >= 0 && pg->panels[p.panel].image)
		return pg->panels[p.panel].image;
	return pg->image;
}

bool nav_rect(const Book* b, Pos p, int rect[4])
{
	if (!paneled(b, p) || p.panel < 0)
		return false;
	const Panel* pn = &b->pages[p.page].panels[p.panel];
	if (pn->image)
		return false;
	rect[0] = pn->x, rect[1] = pn->y, rect[2] = pn->w, rect[3] = pn->h;
	return true;
}

bool nav_next(const Book* b, Pos* p)
{
	if (paneled(b, *p) && p->panel + 1 < b->pages[p->page].npanels) {
		p->panel++;    // overview -> first panel, or panel -> next panel
		return true;
	}
	if (p->page + 1 >= b->npages)
		return false;
	p->page++;
	p->panel = first_panel(b, *p);
	return true;
}

bool nav_prev(const Book* b, Pos* p)
{
	if (paneled(b, *p) && p->panel > 0) {
		p->panel--;
		return true;
	}
	if (paneled(b, *p) && p->panel == 0 && overview) {
		p->panel = PANEL_OVERVIEW;
		return true;
	}
	if (p->page == 0)
		return false;
	p->page--;
	// Going back in panel mode lands on the previous page's last panel.
	int n    = b->pages[p->page].npanels;
	p->panel = (p->mode == MODE_PANEL && n > 0) ? n - 1 : 0;
	return true;
}

void nav_toggle(const Book* b, Pos* p)
{
	(void) b;
	// Page -> panel starts at the first panel: the page was just on screen,
	// so there's no need for its overview. Panel -> page shows the page the
	// panel is on.
	p->mode  = p->mode == MODE_PANEL ? MODE_PAGE : MODE_PANEL;
	p->panel = 0;
}

void nav_clamp(const Book* b, Pos* p)
{
	if (p->mode != MODE_PAGE && p->mode != MODE_PANEL)
		p->mode = MODE_PANEL;
	if (p->page < 0)
		p->page = 0;
	if (p->page >= b->npages)
		p->page = b->npages - 1;
	int n  = b->pages[p->page].npanels;
	int lo = (overview && paneled(b, *p)) ? PANEL_OVERVIEW : 0;
	if (p->panel < lo || p->panel >= n)
		p->panel = 0;
}
