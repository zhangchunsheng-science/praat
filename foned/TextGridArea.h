#ifndef _TextGridArea_h_
#define _TextGridArea_h_
/* TextGridArea.h
 *
 * Copyright (C) 1992-2005,2007-2022 Paul Boersma
 *
 * This code is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * This code is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this work. If not, see <http://www.gnu.org/licenses/>.
 */

#include "FunctionArea.h"
#include "TextGrid.h"
#include "SpellingChecker.h"
#include "Preferences.h"

#include "TextGridArea_enums.h"

Thing_define (TextGridArea, FunctionArea) {
	TextGrid textGrid() const { return static_cast <TextGrid> (our function()); }

	integer selectedTier;

private:
	void v_specializedHighlightSelectionBackground () const
		override;
};

DEFINE_FunctionArea_create (TextGridArea, TextGrid)

/*
	BUG: The following should move to TextGridArea.cpp, once AnyTextGridEditor.cpp no longer needs it.
*/
static integer getSelectedInterval (constTextGridArea me) {
	Melder_assert (my selectedTier >= 1 || my selectedTier <= my textGrid() -> tiers->size);
	const IntervalTier tier = (IntervalTier) my textGrid() -> tiers->at [my selectedTier];
	Melder_assert (tier -> classInfo == classIntervalTier);
	return IntervalTier_timeToIndex (tier, my startSelection());
}

/* End of file TextGridArea.h */
#endif
