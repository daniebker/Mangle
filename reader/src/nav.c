#include "nav.h"

static bool in_panels(const Book* b, Pos p)
{
	return p.mode == MODE_PANEL && b->pages[p.page].npanels > 0;
}

const char* nav_image(const Book* b, Pos p)
{
	const Page* pg = &b->pages[p.page];
	return in_panels(b, p) ? pg->panels[p.panel] : pg->image;
}

bool nav_next(const Book* b, Pos* p)
{
	if (in_panels(b, *p) && p->panel + 1 < b->pages[p->page].npanels) {
		p->panel++;
		return true;
	}
	if (p->page + 1 >= b->npages)
		return false;
	p->page++;
	p->panel = 0;
	return true;
}

bool nav_prev(const Book* b, Pos* p)
{
	if (in_panels(b, *p) && p->panel > 0) {
		p->panel--;
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
	// Page -> panel starts at the first panel of the current page;
	// panel -> page just shows the page the panel is on.
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
	int n = b->pages[p->page].npanels;
	if (p->panel < 0 || p->panel >= n)
		p->panel = 0;
}
