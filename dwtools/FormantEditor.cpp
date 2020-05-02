/* FormantEditor.cpp
 *
 * Copyright (C) 1992-2020 David Weenink
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
/* Erez Volk added FLAC support in 2007 */

#include "FormantEditor.h"
#include "EditorM.h"
#include "SoundEditor.h"
#include "Sound_and_MixingMatrix.h"
#include "Sound_and_Spectrogram.h"
#include "TextGrid_Sound.h"
#include "SpeechSynthesizer_and_TextGrid.h"

Thing_implement (FormantEditor, TimeSoundAnalysisEditor, 0);
Thing_implement (FormantModelerList, Function, 0);

#include "prefs_define.h"
#include "FormantEditor_prefs.h"
#include "prefs_install.h"
#include "FormantEditor_prefs.h"
#include "prefs_copyToInstance.h"
#include "FormantEditor_prefs.h"

void structFormantEditor :: v_info () {
	FormantEditor_Parent :: v_info ();
}

void operator<<= (BOOLVECVU const& target, bool value) {
	for (integer i = 1; i <= target.size; i ++)
		target [i] = value;
}
void operator<<= (INTVECVU const& target, integer value) {
	for (integer i = 1; i <= target.size; i ++)
		target [i] = value;
}

autoINTVEC newINTVECfromString (conststring32 string) {
	autoVEC reals = newVECfromString (string);
	autoINTVEC result = newINTVECraw (reals.size);
	for (integer i = 1; i <= reals.size; i ++) {
		result [i]  = reals [i];
	}
	integer last = reals.size;
	while (result [last] == 0 && last > 0)
		last --;
	Melder_require (last > 0,
		U"There must be at least one integer in the list");
	result.resize (last);
	return result;
}

autoFormantModelerList FormantListWithHistory_to_FormantModelerList (FormantListWithHistory me, double startTime, double endTime, conststring32 numberOfParametersPerTrack_string) {
	try {
		autoFormantModelerList thee = Thing_new (FormantModelerList);
		thy xmin = startTime;
		thy xmax = endTime;
		autoINTVEC numberOfParametersPerTrack = newINTVECfromString (numberOfParametersPerTrack_string);
		Melder_require (numberOfParametersPerTrack.size > 0 ,
			U"The number of items in the parameter list should be larger than zero.");
		integer maximumNumberOfFormants = my formantAnalysisHistory . maximumNumberOfFormants;
		Melder_require (numberOfParametersPerTrack.size <= maximumNumberOfFormants,
			U"The number of items cannot exceed the maximum number of formants (", maximumNumberOfFormants, U").");
		thy numberOfTracksPerModel = thy numberOfParametersPerTrack.size;
		integer numberOfZeros = 0;
		for (integer ipar = 1; ipar <= thy numberOfParametersPerTrack.size; ipar ++) {
			const integer value = thy numberOfParametersPerTrack [ipar];
			Melder_require (value >= 0,
				U"");
			if (value == 0)
				numberOfZeros += 1;
		}
		//Melder_require (thy numberOfParametersPerTrack.size - numberOfZeros > 0,
		//	U"Not all 'number of paramters' should be zero.");
		thy numberOfParametersPerTrack = numberOfParametersPerTrack.move();
		thy numberOfTracksPerModel = thy numberOfParametersPerTrack.size;
		thy numberOfModelers = my numberOfElements;
		for (integer imodel = 1; imodel <= thy numberOfModelers; imodel ++) {
			autoFormantModeler fm = Formant_to_FormantModeler (my formants.at [imodel], startTime, endTime, my ceilings [imodel], thy numberOfParametersPerTrack.get());
			thy formantModelers. addItem_move (fm.move());
		}
		thy selected = newINTVEClinear (thy numberOfModelers, 1, 1);
		return thee;
	} catch (MelderError) {
		Melder_throw (me, U": FormantModelerList not created.");
	}
}

void FormantModelerList_selectAll (FormantModelerList me) {
	my selected.resize (my numberOfModelers);
	INTVEClinear (my selected.get(), 1, 1);
}

static inline void FormantModelerList_setVarianceExponent (FormantModelerList me, double varianceExponent) {
	my varianceExponent = varianceExponent;
}

autoINTVEC FormantModelerList_selectBest3 (FormantModelerList me) {
	/*
		3 The smoothest F1 score
		2 The smoothest F1 & F2 score
		1 the smoothest F1 & F2 & F3 score
	*/
	double smoothnessF1, smoothnessF1F2, smoothnessF1F2F3;
	smoothnessF1 = smoothnessF1F2 = smoothnessF1F2F3 = std::numeric_limits<double>::max();
	autoINTVEC best = newINTVECraw (3);
	for (integer imodel = 1; imodel <= my numberOfModelers; imodel ++) {
		FormantModeler fm = my formantModelers.at [imodel];
		double smoothness = FormantModeler_getSmoothnessValue (fm, 1, 1, 0, my varianceExponent);
		if (smoothness < smoothnessF1) {
			smoothnessF1 = smoothness;
			best [3] = imodel;
		}
		smoothness = FormantModeler_getSmoothnessValue (fm, 1, 2, 0, my varianceExponent);
		if (smoothness < smoothnessF1F2) {
			smoothnessF1F2 = smoothness;
			best [2]  = imodel;
		}
		smoothness = FormantModeler_getSmoothnessValue (fm, 1, 3, 0, my varianceExponent);
		if (smoothness < smoothnessF1F2F3) {
			smoothnessF1F2F3 = smoothness;
			best [1]  = imodel;
		}
	}
	return best;
}

inline integer FormantModelerList_getNumberOfVisible (FormantModelerList me) {
	return my selected.size;
}

void FormantModelerList_getDisplayLayout (FormantModelerList me, integer *out_numberOfRows, integer *out_numberOfColums) {
	const integer numberOfVisible = FormantModelerList_getNumberOfVisible (me);
	integer ncol = 1;
	integer nrow = 3;
	if (numberOfVisible > 3) {
		nrow = 1 + Melder_ifloor (sqrt (numberOfVisible - 0.5));
		ncol = 1 + Melder_ifloor ((numberOfVisible - 1) / nrow);
	}
	if (out_numberOfRows)
		*out_numberOfRows = nrow;
	if (out_numberOfColums)
		*out_numberOfColums = ncol;
}

integer FormantModelerList_getModelerIndexFromRowColumnIndex (FormantModelerList me, integer irow, integer icol) {
	integer numberOfRows, numberOfColums;
	const integer numberOfVisible = FormantModelerList_getNumberOfVisible (me);
	FormantModelerList_getDisplayLayout (me, & numberOfRows, & numberOfColums);
	integer index = (irow - 1) * numberOfColums + icol;
	if (index > numberOfVisible)
		index = 0;
	return ( index > 0 ? my selected [index] : 0 );
}

void FormantModelerList_drawAsMatrix (FormantModelerList me, Graphics g, integer nrow, integer ncol, kGraphicsMatrixOrigin origin, double spaceBetweenFraction_x, double spaceBetweenFraction_y, integer fromFormant, integer toFormant, double fmax, double yGridLineEvery_Hz, double xCursor, double yCursor, integer numberOfParameters, bool drawErrorBars, double barwidth_s, double xTrackOffset_s, bool drawEstimated, bool garnish) {
	if (nrow <= 0 || ncol <= 0)
		FormantModelerList_getDisplayLayout (me, & nrow, & ncol);
	double x1NDC, x2NDC, y1NDC, y2NDC;
	Graphics_inqViewport (g, & x1NDC, & x2NDC, & y1NDC, & y2NDC);
	double fontSize_old = Graphics_inqFontSize (g), fontSize = 8.0;
	auto getXtick = [] (Graphics g, double fontSize) {
		const double margin = 2.8 * fontSize * g -> resolution / 72.0;
		const double wDC = (g -> d_x2DC - g -> d_x1DC) / (g -> d_x2wNDC - g -> d_x1wNDC) * (g -> d_x2NDC - g -> d_x1NDC);
		double dx = 1.5 * margin / wDC;
		double xTick = 0.06 * dx;
		if (dx > 0.4) dx = 0.4;
		return xTick /= 1.0 - 2.0 * dx;
	};
	auto getYtick = [] (Graphics g, double fontSize) {
		double margin = 2.8 * fontSize * g -> resolution / 72.0;
		double hDC = integer_abs (g->d_y2DC - g->d_y1DC) / (g->d_y2wNDC - g->d_y1wNDC) * (g->d_y2NDC - g-> d_y1NDC);
		double dy = margin / hDC;
		double yTick = 0.09 * dy;
		if (dy > 0.4) dy = 0.4;
		yTick /= 1.0 - 2.0 * dy;
		return yTick;
	};
	const bool fillUp = ( origin == kGraphicsMatrixOrigin::BOTTOM_LEFT || origin == kGraphicsMatrixOrigin::BOTTOM_RIGHT );
	const bool rightToLeft = ( origin == kGraphicsMatrixOrigin::TOP_RIGHT || origin ==kGraphicsMatrixOrigin:: BOTTOM_RIGHT );
	const double vp_width = x2NDC - x1NDC, vp_height = y2NDC - y1NDC;
	const double vpi_width = vp_width / (ncol + (ncol - 1) * spaceBetweenFraction_x);
	const double vpi_height = vp_height / (nrow + (nrow - 1) * spaceBetweenFraction_y);
	autoINTVEC best3 = FormantModelerList_selectBest3 (me);
	for (integer id = 1; id <= my selected.size; id ++) {
		const integer irow1 = 1 + (id - 1) / ncol; // left-to-right + top-to-bottom
		const integer icol1 = 1 + (id - 1) % ncol;
		const integer icol = ( rightToLeft ? ncol - icol1 + 1 : icol1 );
		const integer irow = ( fillUp ? nrow - irow1 + 1 : irow1 );
		double vpi_x1 = x1NDC + (icol - 1) * vpi_width * (1.0 + spaceBetweenFraction_x);
		double vpi_x2 = vpi_x1 + vpi_width;
		double vpi_y2 = y2NDC - (irow - 1) * vpi_height * (1.0 + spaceBetweenFraction_y);
		double vpi_y1 = vpi_y2 - vpi_height;
		FormantModeler fm = my formantModelers.at [abs(my selected [id])];
		Graphics_setViewport (g, vpi_x1, vpi_x2, vpi_y1, vpi_y2);
		Graphics_setWindow (g, fm -> xmin, fm -> xmax, 0.0, fmax);
		FormantModeler_speckle_inside (fm, g, fm -> xmin, fm -> xmax, fmax, fromFormant, toFormant,
			drawEstimated, 0, drawErrorBars, barwidth_s, xTrackOffset_s);
		Graphics_setLineWidth (g, 2.0);
		Graphics_setColour (g, ( my selected [id] < 0 ? Melder_BLUE : Melder_BLACK ));
		Graphics_rectangle (g, fm -> xmin, fm -> xmax, 0.0, fmax);
		Graphics_setColour (g, Melder_BLACK);
		Graphics_setLineWidth (g, 1.0);
		/*
			Mark ceilings & smoothness
		*/
		Graphics_setTextAlignment (g, kGraphics_horizontalAlignment::RIGHT, Graphics_HALF);
		Graphics_text (g, fm -> xmax - 0.05 * (fm -> xmax - fm -> xmin),
			fmax - 0.05 * fmax, Melder_integer (fm -> maximumFrequency));
		double w = FormantModeler_getSmoothnessValue (fm, fromFormant, toFormant, 0, my varianceExponent);
		Graphics_setTextAlignment (g, kGraphics_horizontalAlignment::LEFT, Graphics_HALF);
		Graphics_text (g, fm -> xmin + 0.05 * (fm -> xmax - fm -> xmin),
			fmax - 0.05 * fmax, Melder_fixed (w, 2));
		Graphics_setTextAlignment (g, kGraphics_horizontalAlignment::CENTRE, Graphics_HALF);
		autoMelderString best;
		if (best3 [1] == abs (my selected [id]))
			MelderString_append (& best, U"F123");
		if (best3 [2] == abs (my selected [id]))
			MelderString_append (& best, ( best.string && best.string [0] ? U"&F12" : U"F12" ));
		if (best3 [3] == abs (my selected [id]))
			MelderString_append (& best, ( best.string && best.string [0] ? U"&F1" : U"F1" ));
		
		if (best.string && best.string [0]) {
			Graphics_text (g, fm -> xmin + 0.5 * (fm -> xmax - fm -> xmin),
				fmax - 0.05 * fmax, best.string);
			MelderString_empty (& best);
		}
		if (garnish) {
			double xTick = (double) getXtick (g, fontSize) * (fm -> xmax - fm -> xmin);
			double yTick = (double) getYtick (g, fontSize) * (fmax - 0.0);
			if (icol == 1 && irow % 2 == 1) {
				Graphics_setTextAlignment (g, kGraphics_horizontalAlignment::RIGHT, Graphics_HALF);
				Graphics_line (g, fm -> xmin - xTick, fmax, fm -> xmin, fmax);
				Graphics_text (g, fm -> xmin - xTick, fmax, Melder_iround (fmax));
				Graphics_line (g, fm -> xmin - xTick, 0.0, fm -> xmin, 0.0);
				Graphics_text (g, fm -> xmin - xTick, 0.0, U"0.0");
			} else if (icol == ncol && irow % 2 == 0) {
				Graphics_setTextAlignment (g, kGraphics_horizontalAlignment::LEFT, Graphics_HALF);
				Graphics_text (g, fm -> xmax, fmax, Melder_iround (fmax));
				Graphics_text (g, fm -> xmax, 0.0, U"0.0");
			}
			if (irow == 1 && icol % 2 == 0) {
				Graphics_setTextAlignment (g, kGraphics_horizontalAlignment::CENTRE, Graphics_BOTTOM);
				Graphics_line (g, fm -> xmin, fmax, fm -> xmin, fmax + yTick);
				Graphics_text (g, fm -> xmin, fmax + yTick, Melder_fixed (fm -> xmin, 3));
				Graphics_line (g, fm -> xmax, fmax, fm -> xmax, fmax + yTick);
				Graphics_text (g, fm -> xmax, fmax + yTick, Melder_fixed (fm -> xmax, 3));
			} else if (irow == nrow && icol % 2 == 1) {
				Graphics_setTextAlignment (g, kGraphics_horizontalAlignment::CENTRE, Graphics_TOP);
				Graphics_line (g, fm -> xmin, 0.0, fm -> xmin, 0.0 - yTick);
				Graphics_text (g, fm -> xmin, 0.0 - yTick, Melder_fixed (fm -> xmin, 3));
				Graphics_line (g, fm -> xmax, 0.0, fm -> xmax, 0.0 - yTick);
				Graphics_text (g, fm -> xmax, 0.0 - yTick, Melder_fixed (fm -> xmax, 3));
			}
			double yGridLine_Hz = yGridLineEvery_Hz;
			Graphics_setLineType (g, Graphics_DOTTED);
			while (yGridLine_Hz < 0.95 * fmax) {
				Graphics_line (g, fm -> xmin, yGridLine_Hz, fm -> xmax, yGridLine_Hz);
				yGridLine_Hz += yGridLineEvery_Hz;
			}
			/*
				Cursors
			*/
			Graphics_setColour (g, Melder_RED);
			Graphics_setLineType (g, Graphics_DASHED);
			if (xCursor > fm -> xmin && xCursor <= fm -> xmax)
				Graphics_line (g, xCursor, 0.0, xCursor, fmax);
			if (yCursor > 0.0 && yCursor < fmax)
				Graphics_line (g, fm -> xmin, yCursor, fm -> xmax, yCursor);
			Graphics_setColour (g, Melder_BLACK);
			Graphics_setLineType (g, Graphics_DRAWN);
		}
	}
	Graphics_setFontSize (g, fontSize_old);
	Graphics_setViewport (g, x1NDC, x2NDC, y1NDC, y2NDC);
}
/********** UTILITIES **********/

static double _FormantEditor_computeSoundY (FormantEditor me) {
	const TextGrid grid = my masterSlave.get();
	const integer numberOfTiers = grid -> tiers->size;
	bool showAnalysis = my v_hasAnalysis () &&
			(my p_spectrogram_show || my p_pitch_show || my p_intensity_show || my p_formant_show) &&
			(my d_longSound.data || my d_sound.data);
	integer numberOfVisibleChannels =
		my d_sound.data ? (my d_sound.data -> ny > 8 ? 8 : my d_sound.data -> ny) :
		my d_longSound.data ? (my d_longSound.data -> numberOfChannels > 8 ? 8 : my d_longSound.data -> numberOfChannels) : 1;
	return my d_sound.data || my d_longSound.data ? numberOfTiers / (2.0 * numberOfVisibleChannels + numberOfTiers * (showAnalysis ? 1.8 : 1.3)) : 1.0;
}

static void _AnyTier_identifyClass (Function anyTier, IntervalTier *intervalTier, TextTier *textTier) {
	if (anyTier -> classInfo == classIntervalTier) {
		*intervalTier = (IntervalTier) anyTier;
		*textTier = nullptr;
	} else {
		*intervalTier = nullptr;
		*textTier = (TextTier) anyTier;
	}
}

static integer _FormantEditor_yWCtoTier (FormantEditor me, double yWC) {
	const TextGrid grid = my masterSlave.get();
	const integer numberOfTiers = grid -> tiers->size;
	const double soundY = _FormantEditor_computeSoundY (me);
	integer tierNumber = numberOfTiers - Melder_ifloor (yWC / soundY * (double) numberOfTiers);
	if (tierNumber < 1)
		tierNumber = 1;
	if (tierNumber > numberOfTiers)
		tierNumber = numberOfTiers;
	return tierNumber;
}

static void _FormantEditor_timeToInterval (FormantEditor me, double t, integer tierNumber,
	double *tmin, double *tmax)
{
	const TextGrid grid = my masterSlave.get();
	const Function tier = grid -> tiers->at [tierNumber];
	IntervalTier intervalTier;
	TextTier textTier;
	_AnyTier_identifyClass (tier, & intervalTier, & textTier);
	if (intervalTier) {
		integer iinterval = IntervalTier_timeToIndex (intervalTier, t);
		if (iinterval == 0) {
			if (t < my tmin) {
				iinterval = 1;
			} else {
				iinterval = intervalTier -> intervals.size;
			}
		}
		Melder_assert (iinterval >= 1);
		Melder_assert (iinterval <= intervalTier -> intervals.size);
		const TextInterval interval = intervalTier -> intervals.at [iinterval];
		*tmin = interval -> xmin;
		*tmax = interval -> xmax;
	} else {
		const integer n = textTier -> points.size;
		if (n == 0) {
			*tmin = my tmin;
			*tmax = my tmax;
		} else {
			integer ipointleft = AnyTier_timeToLowIndex (textTier->asAnyTier(), t);
			*tmin = ipointleft == 0 ? my tmin : textTier -> points.at [ipointleft] -> number;
			*tmax = ipointleft == n ? my tmax : textTier -> points.at [ipointleft + 1] -> number;
		}
	}
	if (*tmin < my tmin)
		*tmin = my tmin;   // clip by FunctionEditor's time domain
	if (*tmax > my tmax)
		*tmax = my tmax;
}

static void checkTierSelection (FormantEditor me, conststring32 verbPhrase) {
	const TextGrid grid = my masterSlave.get();
	if (my selectedTier < 1 || my selectedTier > grid -> tiers->size)
		Melder_throw (U"To ", verbPhrase, U", first select a tier by clicking anywhere inside it.");
}

static integer getSelectedInterval (FormantEditor me) {
	const TextGrid grid = my masterSlave.get();
	Melder_assert (my selectedTier >= 1 || my selectedTier <= grid -> tiers->size);
	const IntervalTier tier = (IntervalTier) grid -> tiers->at [my selectedTier];
	Melder_assert (tier -> classInfo == classIntervalTier);
	return IntervalTier_timeToIndex (tier, my startSelection);
}

static integer getSelectedLeftBoundary (FormantEditor me) {
	const TextGrid grid = my masterSlave.get();
	Melder_assert (my selectedTier >= 1 || my selectedTier <= grid -> tiers->size);
	const IntervalTier tier = (IntervalTier) grid -> tiers->at [my selectedTier];
	Melder_assert (tier -> classInfo == classIntervalTier);
	return IntervalTier_hasBoundary (tier, my startSelection);
}

static integer getSelectedPoint (FormantEditor me) {
	const TextGrid grid = my masterSlave.get();
	Melder_assert (my selectedTier >= 1 || my selectedTier <= grid -> tiers->size);
	const TextTier tier = (TextTier) grid -> tiers->at [my selectedTier];
	Melder_assert (tier -> classInfo == classTextTier);
	return AnyTier_hasPoint (tier->asAnyTier(), my startSelection);
}

static void scrollToView (FormantEditor me, double t) {
	if (t <= my startWindow) {
		FunctionEditor_shift (me, t - my startWindow - 0.618 * (my endWindow - my startWindow), true);
	} else if (t >= my endWindow) {
		FunctionEditor_shift (me, t - my endWindow + 0.618 * (my endWindow - my startWindow), true);
	} else {
		FunctionEditor_marksChanged (me, true);
	}
}

/********** METHODS **********/

/*
 * The main invariant of the FormantEditor is that the selected interval
 * always has the cursor in it, and that the cursor always selects an interval
 * if the selected tier is an interval tier.
 */

/***** FILE MENU *****/

static void menu_cb_ExtractSelectedTextGrid_preserveTimes (FormantEditor me, EDITOR_ARGS_DIRECT) {
	if (my endSelection <= my startSelection)
		Melder_throw (U"No selection.");
	autoTextGrid extract = TextGrid_extractPart (my masterSlave.get(), my startSelection, my endSelection, true);
	Editor_broadcastPublication (me, extract.move());
}

static void menu_cb_ExtractSelectedTextGrid_timeFromZero (FormantEditor me, EDITOR_ARGS_DIRECT) {
	if (my endSelection <= my startSelection)
		Melder_throw (U"No selection.");
	autoTextGrid extract = TextGrid_extractPart (my masterSlave.get(), my startSelection, my endSelection, false);
	Editor_broadcastPublication (me, extract.move());
}

void structFormantEditor :: v_createMenuItems_file_extract (EditorMenu menu) {
	FormantEditor_Parent :: v_createMenuItems_file_extract (menu);
	extractSelectedTextGridPreserveTimesButton =
		EditorMenu_addCommand (menu, U"Extract selected TextGrid (preserve times)", 0, menu_cb_ExtractSelectedTextGrid_preserveTimes);
	extractSelectedTextGridTimeFromZeroButton =
		EditorMenu_addCommand (menu, U"Extract selected TextGrid (time from 0)", 0, menu_cb_ExtractSelectedTextGrid_timeFromZero);
}

static void menu_cb_WriteToTextFile (FormantEditor me, EDITOR_ARGS_FORM) {
	EDITOR_FORM_SAVE (U"Save as TextGrid text file", nullptr)
		Melder_sprint (defaultName,300, my masterSlave -> name.get(), U".TextGrid");
	EDITOR_DO_SAVE
		Data_writeToTextFile (my masterSlave.get(), file);
	EDITOR_END
}

void structFormantEditor :: v_createMenuItems_file_write (EditorMenu menu) {
	FormantEditor_Parent :: v_createMenuItems_file_write (menu);
	EditorMenu_addCommand (menu, U"Save TextGrid as text file...", 'S', menu_cb_WriteToTextFile);
}

static void menu_cb_DrawVisibleTextGrid (FormantEditor me, EDITOR_ARGS_FORM) {
	EDITOR_FORM (U"Draw visible TextGrid", nullptr)
		my v_form_pictureWindow (cmd);
		my v_form_pictureMargins (cmd);
		my v_form_pictureSelection (cmd);
		BOOLEAN (garnish, U"Garnish", my default_picture_garnish ())
	EDITOR_OK
		my v_ok_pictureWindow (cmd);
		my v_ok_pictureMargins (cmd);
		my v_ok_pictureSelection (cmd);
		SET_BOOLEAN (garnish, my pref_picture_garnish ())
	EDITOR_DO
		my v_do_pictureWindow (cmd);
		my v_do_pictureMargins (cmd);
		my v_do_pictureSelection (cmd);
		my pref_picture_garnish () = garnish;
		Editor_openPraatPicture (me);
		TextGrid_Sound_draw (my masterSlave.get(), nullptr, my pictureGraphics, my startWindow, my endWindow, true, my p_useTextStyles,
			my pref_picture_garnish ());
		FunctionEditor_garnish (me);
		Editor_closePraatPicture (me);
	EDITOR_END
}

static void menu_cb_DrawVisibleSoundAndTextGrid (FormantEditor me, EDITOR_ARGS_FORM) {
	EDITOR_FORM (U"Draw visible sound and TextGrid", nullptr)
		my v_form_pictureWindow (cmd);
		my v_form_pictureMargins (cmd);
		my v_form_pictureSelection (cmd);
		BOOLEAN (garnish, U"Garnish", my default_picture_garnish ())
	EDITOR_OK
		my v_ok_pictureWindow (cmd);
		my v_ok_pictureMargins (cmd);
		my v_ok_pictureSelection (cmd);
		SET_BOOLEAN (garnish, my pref_picture_garnish ())
	EDITOR_DO
		my v_do_pictureWindow (cmd);
		my v_do_pictureMargins (cmd);
		my v_do_pictureSelection (cmd);
		my pref_picture_garnish () = garnish;
		Editor_openPraatPicture (me);
		{// scope
			autoSound sound = my d_longSound.data ?
				LongSound_extractPart (my d_longSound.data, my startWindow, my endWindow, true) :
				Sound_extractPart (my d_sound.data, my startWindow, my endWindow,
					kSound_windowShape::RECTANGULAR, 1.0, true);
			TextGrid_Sound_draw (my masterSlave.get(), sound.get(), my pictureGraphics,
				my startWindow, my endWindow, true, my p_useTextStyles, my pref_picture_garnish ());
		}
		FunctionEditor_garnish (me);
		Editor_closePraatPicture (me);
	EDITOR_END
}

void structFormantEditor :: v_createMenuItems_file_draw (EditorMenu menu) {
	FormantEditor_Parent :: v_createMenuItems_file_draw (menu);
	EditorMenu_addCommand (menu, U"Draw visible TextGrid...", 0, menu_cb_DrawVisibleTextGrid);
	if (d_sound.data || d_longSound.data)
		EditorMenu_addCommand (menu, U"Draw visible sound and TextGrid...", 0, menu_cb_DrawVisibleSoundAndTextGrid);
}

/***** EDIT MENU *****/

#ifndef macintosh
static void menu_cb_Cut (FormantEditor me, EDITOR_ARGS_DIRECT) {
	GuiText_cut (my text);
}
static void menu_cb_Copy (FormantEditor me, EDITOR_ARGS_DIRECT) {
	GuiText_copy (my text);
}
static void menu_cb_Paste (FormantEditor me, EDITOR_ARGS_DIRECT) {
	GuiText_paste (my text);
}
static void menu_cb_Erase (FormantEditor me, EDITOR_ARGS_DIRECT) {
	GuiText_remove (my text);
}
#endif

/***** QUERY MENU *****/

static void menu_cb_GetStartingPointOfInterval (FormantEditor me, EDITOR_ARGS_DIRECT) {
	const TextGrid grid = my masterSlave.get();
	checkTierSelection (me, U"query the starting point of an interval");
	const Function anyTier = grid -> tiers->at [my selectedTier];
	if (anyTier -> classInfo == classIntervalTier) {
		const IntervalTier tier = (IntervalTier) anyTier;
		const integer iinterval = IntervalTier_timeToIndex (tier, my startSelection);
		const double time = ( iinterval < 1 || iinterval > tier -> intervals.size ? undefined :
				tier -> intervals.at [iinterval] -> xmin );
		Melder_informationReal (time, U"seconds");
	} else {
		Melder_throw (U"The selected tier is not an interval tier.");
	}
}

static void menu_cb_GetEndPointOfInterval (FormantEditor me, EDITOR_ARGS_DIRECT) {
	const TextGrid grid = my masterSlave.get();
	checkTierSelection (me, U"query the end point of an interval");
	const Function anyTier = grid -> tiers->at [my selectedTier];
	if (anyTier -> classInfo == classIntervalTier) {
		const IntervalTier tier = (IntervalTier) anyTier;
		const integer iinterval = IntervalTier_timeToIndex (tier, my startSelection);
		const double time = ( iinterval < 1 || iinterval > tier -> intervals.size ? undefined :
				tier -> intervals.at [iinterval] -> xmax );
		Melder_informationReal (time, U"seconds");
	} else {
		Melder_throw (U"The selected tier is not an interval tier.");
	}
}

static void menu_cb_GetLabelOfInterval (FormantEditor me, EDITOR_ARGS_DIRECT) {
	const TextGrid grid = my masterSlave.get();
	checkTierSelection (me, U"query the label of an interval");
	const Function anyTier = grid -> tiers->at [my selectedTier];
	if (anyTier -> classInfo == classIntervalTier) {
		const IntervalTier tier = (IntervalTier) anyTier;
		const integer iinterval = IntervalTier_timeToIndex (tier, my startSelection);
		const conststring32 label = ( iinterval < 1 || iinterval > tier -> intervals.size ? U"" :
				tier -> intervals.at [iinterval] -> text.get() );
		Melder_information (label);
	} else {
		Melder_throw (U"The selected tier is not an interval tier.");
	}
}

/***** VIEW MENU *****/

static void do_selectAdjacentTier (FormantEditor me, bool previous) {
	const TextGrid grid = my masterSlave.get();
	const integer n = grid -> tiers->size;
	if (n >= 2) {
		my selectedTier = ( previous ?
				my selectedTier > 1 ? my selectedTier - 1 : n :
				my selectedTier < n ? my selectedTier + 1 : 1 );
		_FormantEditor_timeToInterval (me, my startSelection, my selectedTier, & my startSelection, & my endSelection);
		FunctionEditor_marksChanged (me, true);
	}
}

static void menu_cb_SelectPreviousTier (FormantEditor me, EDITOR_ARGS_DIRECT) {
	do_selectAdjacentTier (me, true);
}

static void menu_cb_SelectNextTier (FormantEditor me, EDITOR_ARGS_DIRECT) {
	do_selectAdjacentTier (me, false);
}

static void do_selectAdjacentInterval (FormantEditor me, bool previous, bool shift) {
	const TextGrid grid = my masterSlave.get();
	IntervalTier intervalTier;
	TextTier textTier;
	if (my selectedTier < 1 || my selectedTier > grid -> tiers->size)
		return;
	_AnyTier_identifyClass (grid -> tiers->at [my selectedTier], & intervalTier, & textTier);
	if (intervalTier) {
		const integer n = intervalTier -> intervals.size;
		if (n >= 2) {
			integer iinterval = IntervalTier_timeToIndex (intervalTier, my startSelection);
			if (shift) {
				const integer binterval = IntervalTier_timeToIndex (intervalTier, my startSelection);
				integer einterval = IntervalTier_timeToIndex (intervalTier, my endSelection);
				if (my endSelection == intervalTier -> xmax)
					einterval ++;
				if (binterval < iinterval && einterval > iinterval + 1) {
					const TextInterval interval = intervalTier -> intervals.at [iinterval];
					my startSelection = interval -> xmin;
					my endSelection = interval -> xmax;
				} else if (previous) {
					if (einterval > iinterval + 1) {
						if (einterval <= n + 1) {
							const TextInterval interval = intervalTier -> intervals.at [einterval - 1];
							my endSelection = interval -> xmin;
						}
					} else if (binterval > 1) {
						const TextInterval interval = intervalTier -> intervals.at [binterval - 1];
						my startSelection = interval -> xmin;
					}
				} else {
					if (binterval < iinterval) {
						if (binterval > 0) {
							const TextInterval interval = intervalTier -> intervals.at [binterval];
							my startSelection = interval -> xmax;
						}
					} else if (einterval <= n) {
						const TextInterval interval = intervalTier -> intervals.at [einterval];
						my endSelection = interval -> xmax;
					}
				}
			} else {
				iinterval = ( previous ?
						iinterval > 1 ? iinterval - 1 : n :
						iinterval < n ? iinterval + 1 : 1 );
				const TextInterval interval = intervalTier -> intervals.at [iinterval];
				my startSelection = interval -> xmin;
				my endSelection = interval -> xmax;
			}
			scrollToView (me, iinterval == n ? my startSelection : iinterval == 1 ? my endSelection : (my startSelection + my endSelection) / 2);
		}
	} else {
		const integer n = textTier -> points.size;
		if (n >= 2) {
			integer ipoint = AnyTier_timeToHighIndex (textTier->asAnyTier(), my startSelection);
			ipoint = ( previous ?
					ipoint > 1 ? ipoint - 1 : n :
					ipoint < n ? ipoint + 1 : 1 );
			const TextPoint point = textTier -> points.at [ipoint];
			my startSelection = my endSelection = point -> number;
			scrollToView (me, my startSelection);
		}
	}
}

static void menu_cb_SelectPreviousInterval (FormantEditor me, EDITOR_ARGS_DIRECT) {
	do_selectAdjacentInterval (me, true, false);
}

static void menu_cb_SelectNextInterval (FormantEditor me, EDITOR_ARGS_DIRECT) {
	do_selectAdjacentInterval (me, false, false);
}

static void menu_cb_ExtendSelectPreviousInterval (FormantEditor me, EDITOR_ARGS_DIRECT) {
	do_selectAdjacentInterval (me, true, true);
}

static void menu_cb_ExtendSelectNextInterval (FormantEditor me, EDITOR_ARGS_DIRECT) {
	do_selectAdjacentInterval (me, false, true);
}

static void menu_cb_MoveBtoZero (FormantEditor me, EDITOR_ARGS_DIRECT) {
	const double zero = Sound_getNearestZeroCrossing (my d_sound.data, my startSelection, 1);   // STEREO BUG
	if (isdefined (zero)) {
		my startSelection = zero;
		if (my startSelection > my endSelection)
			std::swap (my startSelection, my endSelection);
		FunctionEditor_marksChanged (me, true);
	}
}

static void menu_cb_MoveCursorToZero (FormantEditor me, EDITOR_ARGS_DIRECT) {
	const double zero = Sound_getNearestZeroCrossing (my d_sound.data, 0.5 * (my startSelection + my endSelection), 1);   // STEREO BUG
	if (isdefined (zero)) {
		my startSelection = my endSelection = zero;
		FunctionEditor_marksChanged (me, true);
	}
}

static void menu_cb_MoveEtoZero (FormantEditor me, EDITOR_ARGS_DIRECT) {
	const double zero = Sound_getNearestZeroCrossing (my d_sound.data, my endSelection, 1);   // STEREO BUG
	if (isdefined (zero)) {
		my endSelection = zero;
		if (my startSelection > my endSelection)
			std::swap (my startSelection, my endSelection);
		FunctionEditor_marksChanged (me, true);
	}
}

/***** PITCH MENU *****/

static void menu_cb_DrawTextGridAndPitch (FormantEditor me, EDITOR_ARGS_FORM) {
	EDITOR_FORM (U"Draw TextGrid and Pitch separately", nullptr)
		my v_form_pictureWindow (cmd);
		LABEL (U"TextGrid:")
		BOOLEAN (showBoundariesAndPoints, U"Show boundaries and points", my default_picture_showBoundaries ());
		LABEL (U"Pitch:")
		BOOLEAN (speckle, U"Speckle", my default_picture_pitch_speckle ());
		my v_form_pictureMargins (cmd);
		my v_form_pictureSelection (cmd);
		BOOLEAN (garnish, U"Garnish", my default_picture_garnish ());
	EDITOR_OK
		my v_ok_pictureWindow (cmd);
		SET_BOOLEAN (showBoundariesAndPoints, my pref_picture_showBoundaries ())
		SET_BOOLEAN (speckle, my pref_picture_pitch_speckle ())
		my v_ok_pictureMargins (cmd);
		my v_ok_pictureSelection (cmd);
		SET_BOOLEAN (garnish, my pref_picture_garnish ())
	EDITOR_DO
		my v_do_pictureWindow (cmd);
		my pref_picture_showBoundaries () = showBoundariesAndPoints;
		my pref_picture_pitch_speckle () = speckle;
		my v_do_pictureMargins (cmd);
		my v_do_pictureSelection (cmd);
		my pref_picture_garnish () = garnish;
		if (! my p_pitch_show)
			Melder_throw (U"No pitch contour is visible.\nFirst choose \"Show pitch\" from the Pitch menu.");
		if (! my d_pitch) {
			TimeSoundAnalysisEditor_computePitch (me);
			if (! my d_pitch)
				Melder_throw (U"Cannot compute pitch.");
		}
		Editor_openPraatPicture (me);
		double pitchFloor_hidden = Function_convertStandardToSpecialUnit (my d_pitch.get(), my p_pitch_floor, Pitch_LEVEL_FREQUENCY, (int) my p_pitch_unit);
		double pitchCeiling_hidden = Function_convertStandardToSpecialUnit (my d_pitch.get(), my p_pitch_ceiling, Pitch_LEVEL_FREQUENCY, (int) my p_pitch_unit);
		double pitchFloor_overt = Function_convertToNonlogarithmic (my d_pitch.get(), pitchFloor_hidden, Pitch_LEVEL_FREQUENCY, (int) my p_pitch_unit);
		double pitchCeiling_overt = Function_convertToNonlogarithmic (my d_pitch.get(), pitchCeiling_hidden, Pitch_LEVEL_FREQUENCY, (int) my p_pitch_unit);
		double pitchViewFrom_overt = ( my p_pitch_viewFrom < my p_pitch_viewTo ? my p_pitch_viewFrom : pitchFloor_overt );
		double pitchViewTo_overt = ( my p_pitch_viewFrom < my p_pitch_viewTo ? my p_pitch_viewTo : pitchCeiling_overt );
		TextGrid_Pitch_drawSeparately (my masterSlave.get(), my d_pitch.get(), my pictureGraphics, my startWindow, my endWindow,
			pitchViewFrom_overt, pitchViewTo_overt, showBoundariesAndPoints, my p_useTextStyles, garnish,
			speckle, my p_pitch_unit
		);
		FunctionEditor_garnish (me);
		Editor_closePraatPicture (me);
	EDITOR_END
}

/***** INTERVAL MENU *****/

static void insertBoundaryOrPoint (FormantEditor me, integer itier, double t1, double t2, bool insertSecond) {
	const TextGrid grid = my masterSlave.get();
	const integer numberOfTiers = grid -> tiers->size;
	if (itier < 1 || itier > numberOfTiers)
		Melder_throw (U"No tier ", itier, U".");
	IntervalTier intervalTier;
	TextTier textTier;
	_AnyTier_identifyClass (grid -> tiers->at [itier], & intervalTier, & textTier);
	Melder_assert (t1 <= t2);

	if (intervalTier) {
		autoTextInterval rightNewInterval, midNewInterval;
		const bool t1IsABoundary = IntervalTier_hasTime (intervalTier, t1);
		const bool t2IsABoundary = IntervalTier_hasTime (intervalTier, t2);
		if (t1 == t2 && t1IsABoundary)
			Melder_throw (U"Cannot add a boundary at ", Melder_fixed (t1, 6), U" seconds, because there is already a boundary there.");
		if (t1IsABoundary && t2IsABoundary)
			Melder_throw (U"Cannot add boundaries at ", Melder_fixed (t1, 6), U" and ", Melder_fixed (t2, 6), U" seconds, because there are already boundaries there.");
		const integer iinterval = IntervalTier_timeToIndex (intervalTier, t1);
		const integer iinterval2 = t1 == t2 ? iinterval : IntervalTier_timeToIndex (intervalTier, t2);
		if (iinterval == 0 || iinterval2 == 0)
			Melder_throw (U"The selection is outside the time domain of the intervals.");
		const integer correctedIinterval2 = ( t2IsABoundary && iinterval2 == intervalTier -> intervals.size ? iinterval2 + 1 : iinterval2 );
		if (correctedIinterval2 > iinterval + 1 || (correctedIinterval2 > iinterval && ! t2IsABoundary))
			Melder_throw (U"The selection straddles a boundary.");
		const TextInterval interval = intervalTier -> intervals.at [iinterval];

		if (t1 == t2) {
			Editor_save (me, U"Add boundary");
		} else {
			Editor_save (me, U"Add interval");
		}

		if (itier == my selectedTier) {
			/*
				Divide up the label text into left, mid and right, depending on where the text selection is.
			*/
			autostring32 text = Melder_dup (interval -> text.get());
			rightNewInterval = TextInterval_create (t2, interval -> xmax, text.get());
			midNewInterval = TextInterval_create (t1, t2, text.get());
			TextInterval_setText (interval, U"");
		} else {
			/*
				Move the text to the left of the boundary.
			*/
			rightNewInterval = TextInterval_create (t2, interval -> xmax, U"");
			midNewInterval = TextInterval_create (t1, t2, U"");
		}
		if (t1IsABoundary) {
			/*
				Merge mid with left interval.
			*/
			if (interval -> xmin != t1)
				Melder_fatal (U"Boundary unequal: ", interval -> xmin, U" versus ", t1, U".");
			interval -> xmax = t2;
			TextInterval_setText (interval, Melder_cat (interval -> text.get(), midNewInterval -> text.get()));
		} else if (t2IsABoundary) {
			/*
				Merge mid and right interval.
			*/
			if (interval -> xmax != t2)
				Melder_fatal (U"Boundary unequal: ", interval -> xmax, U" versus ", t2, U".");
			interval -> xmax = t1;
			Melder_assert (rightNewInterval -> xmin == t2);
			Melder_assert (rightNewInterval -> xmax == t2);
			rightNewInterval -> xmin = t1;
			TextInterval_setText (rightNewInterval.get(), Melder_cat (midNewInterval -> text.get(), rightNewInterval -> text.get()));
		} else {
			interval -> xmax = t1;
			if (t1 != t2)
				intervalTier -> intervals.addItem_move (midNewInterval.move());
		}
		intervalTier -> intervals.addItem_move (rightNewInterval.move());
		if (insertSecond && numberOfTiers >= 2 && t1 == t2) {
			/*
				Find the last time before t on another tier.
			*/
			double tlast = interval -> xmin;
			for (integer jtier = 1; jtier <= numberOfTiers; jtier ++) {
				if (jtier != itier) {
					double tmin, tmax;
					_FormantEditor_timeToInterval (me, t1, jtier, & tmin, & tmax);
					if (tmin > tlast)
						tlast = tmin;
				}
			}
			if (tlast > interval -> xmin && tlast < t1) {
				autoTextInterval newInterval = TextInterval_create (tlast, t1, U"");
				interval -> xmax = tlast;
				intervalTier -> intervals.addItem_move (newInterval.move());
			}
		}
	} else {
		if (AnyTier_hasPoint (textTier->asAnyTier(), t1))
			Melder_throw (U"Cannot add a point at ", Melder_fixed (t1, 6), U" seconds, because there is already a point there.");

		Editor_save (me, U"Add point");

		autoTextPoint newPoint = TextPoint_create (t1, U"");
		textTier -> points. addItem_move (newPoint.move());
	}
	my startSelection = my endSelection = t1;
}

static void do_insertIntervalOnTier (FormantEditor me, int itier) {
	try {
		insertBoundaryOrPoint (me, itier,
				my playingCursor || my playingSelection ? my playCursor : my startSelection,
				my playingCursor || my playingSelection ? my playCursor : my endSelection,
				true);
		my selectedTier = itier;
		FunctionEditor_marksChanged (me, true);
		Editor_broadcastDataChanged (me);
	} catch (MelderError) {
		Melder_throw (U"Interval not inserted.");
	}
}

static void menu_cb_InsertIntervalOnSlaveTier (FormantEditor me, EDITOR_ARGS_DIRECT) { do_insertIntervalOnTier (me, 1); }

static void menu_cb_AlignInterval (FormantEditor me, EDITOR_ARGS_DIRECT) {
	const TextGrid grid = my masterSlave.get();
	checkTierSelection (me, U"align words");
	const AnyTier tier = static_cast <AnyTier> (grid -> tiers->at [my selectedTier]);
	if (tier -> classInfo != classIntervalTier)
		Melder_throw (U"Alignment works only for interval tiers, whereas tier ", my selectedTier, U" is a point tier.\nSelect an interval tier instead.");
	integer intervalNumber = getSelectedInterval (me);
	if (! intervalNumber)
		Melder_throw (U"Select an interval first");
	if (! my p_align_includeWords && ! my p_align_includePhonemes)
		Melder_throw (U"Nothing to be done.\nPlease switch on \"Include words\" and/or \"Include phonemes\" in the \"Alignment settings\".");
	{// scope
		const autoMelderProgressOff noprogress;
		Function anySound = my d_sound.data;
		if (my d_longSound.data)
			anySound = my d_longSound.data;
		Editor_save (me, U"Align interval");
		TextGrid_anySound_alignInterval (grid, anySound, my selectedTier, intervalNumber,
			my p_align_language, my p_align_includeWords, my p_align_includePhonemes);
	}
	FunctionEditor_redraw (me);
	Editor_broadcastDataChanged (me);
}

static void menu_cb_AlignmentSettings (FormantEditor me, EDITOR_ARGS_FORM) {
	EDITOR_FORM (U"Alignment settings", nullptr)
		OPTIONMENU (language, U"Language", (int) Strings_findString (espeakdata_languages_names.get(), U"English (Great Britain)"))
		for (integer i = 1; i <= espeakdata_languages_names -> numberOfStrings; i ++) {
			OPTION ((conststring32) espeakdata_languages_names -> strings [i].get());
		}
		BOOLEAN (includeWords,    U"Include words",    my default_align_includeWords ())
		BOOLEAN (includePhonemes, U"Include phonemes", my default_align_includePhonemes ())
		BOOLEAN (allowSilences,   U"Allow silences",   my default_align_allowSilences ())
	EDITOR_OK
		int prefVoice = (int) Strings_findString (espeakdata_languages_names.get(), my p_align_language);
		if (prefVoice == 0) prefVoice = (int) Strings_findString (espeakdata_languages_names.get(), U"English (Great Britain)");
		SET_OPTION (language, prefVoice)
		SET_BOOLEAN (includeWords, my p_align_includeWords)
		SET_BOOLEAN (includePhonemes, my p_align_includePhonemes)
		SET_BOOLEAN (allowSilences, my p_align_allowSilences)
	EDITOR_DO
		pref_str32cpy2 (my pref_align_language (), my p_align_language, espeakdata_languages_names -> strings [language].get());
		my pref_align_includeWords    () = my p_align_includeWords    = includeWords;
		my pref_align_includePhonemes () = my p_align_includePhonemes = includePhonemes;
		my pref_align_allowSilences   () = my p_align_allowSilences   = allowSilences;
	EDITOR_END
}

/***** BOUNDARY/POINT MENU *****/

static void menu_cb_RemovePointOrBoundary (FormantEditor me, EDITOR_ARGS_DIRECT) {
	const TextGrid grid =  my masterSlave.get();
	checkTierSelection (me, U"remove a point or boundary");
	const Function anyTier = grid -> tiers->at [my selectedTier];
	if (anyTier -> classInfo == classIntervalTier) {
		const IntervalTier tier = (IntervalTier) anyTier;
		const integer selectedLeftBoundary = getSelectedLeftBoundary (me);
		if (selectedLeftBoundary == 0)
			Melder_throw (U"To remove a boundary, first click on it.");

		Editor_save (me, U"Remove boundary");
		IntervalTier_removeLeftBoundary (tier, selectedLeftBoundary);
	} else {
		const TextTier tier = (TextTier) anyTier;
		const integer selectedPoint = getSelectedPoint (me);
		if (selectedPoint == 0)
			Melder_throw (U"To remove a point, first click on it.");

		Editor_save (me, U"Remove point");
		tier -> points. removeItem (selectedPoint);
	}
	FunctionEditor_updateText (me);
	FunctionEditor_redraw (me);
	Editor_broadcastDataChanged (me);
}

static void do_movePointOrBoundary (FormantEditor me, int where) {
	const TextGrid grid =  my masterSlave.get();
	if (where == 0 && ! my d_sound.data)
		return;
	checkTierSelection (me, U"move a point or boundary");
	const Function anyTier = grid -> tiers->at [my selectedTier];
	if (anyTier -> classInfo == classIntervalTier) {
		const IntervalTier tier = (IntervalTier) anyTier;
		static const conststring32 boundarySaveText [3] { U"Move boundary to zero crossing", U"Move boundary to B", U"Move boundary to E" };
		const integer selectedLeftBoundary = getSelectedLeftBoundary (me);
		if (selectedLeftBoundary == 0)
			Melder_throw (U"To move a boundary, first click on it.");
		const TextInterval left = tier -> intervals.at [selectedLeftBoundary - 1];
		const TextInterval right = tier -> intervals.at [selectedLeftBoundary];
		const double position = ( where == 1 ? my startSelection : where == 2 ? my endSelection :
				Sound_getNearestZeroCrossing (my d_sound.data, left -> xmax, 1) );   // STEREO BUG
		if (isundef (position))
			Melder_throw (U"There is no zero crossing to move to.");
		if (position <= left -> xmin || position >= right -> xmax)
			Melder_throw (U"Cannot move a boundary past its neighbour.");

		Editor_save (me, boundarySaveText [where]);

		left -> xmax = right -> xmin = my startSelection = my endSelection = position;
	} else {
		TextTier tier = (TextTier) anyTier;
		static const conststring32 pointSaveText [3] { U"Move point to zero crossing", U"Move point to B", U"Move point to E" };
		const integer selectedPoint = getSelectedPoint (me);
		if (selectedPoint == 0)
			Melder_throw (U"To move a point, first click on it.");
		const TextPoint point = tier -> points.at [selectedPoint];
		const double position = ( where == 1 ? my startSelection : where == 2 ? my endSelection :
				Sound_getNearestZeroCrossing (my d_sound.data, point -> number, 1) );   // STEREO BUG
		if (isundef (position))
			Melder_throw (U"There is no zero crossing to move to.");

		Editor_save (me, pointSaveText [where]);

		point -> number = my startSelection = my endSelection = position;
	}
	FunctionEditor_marksChanged (me, true);   // because cursor has moved
	Editor_broadcastDataChanged (me);
}

static void menu_cb_MoveToB (FormantEditor me, EDITOR_ARGS_DIRECT) {
	do_movePointOrBoundary (me, 1);
}

static void menu_cb_MoveToE (FormantEditor me, EDITOR_ARGS_DIRECT) {
	do_movePointOrBoundary (me, 2);
}

static void menu_cb_MoveToZero (FormantEditor me, EDITOR_ARGS_DIRECT) {
	do_movePointOrBoundary (me, 0);
}

static void do_insertOnTier (FormantEditor me, integer itier) {
	try {
		insertBoundaryOrPoint (me, itier,
			my playingCursor || my playingSelection ? my playCursor : my startSelection,
			my playingCursor || my playingSelection ? my playCursor : my endSelection,
			false
		);
		my selectedTier = itier;
		FunctionEditor_marksChanged (me, true);
		Editor_broadcastDataChanged (me);
	} catch (MelderError) {
		Melder_throw (U"Boundary or point not inserted.");
	}
}

static void menu_cb_InsertOnSelectedTier (FormantEditor me, EDITOR_ARGS_DIRECT) {
	do_insertOnTier (me, my selectedTier);
}

static void menu_cb_InsertOnTier1 (FormantEditor me, EDITOR_ARGS_DIRECT) { do_insertOnTier (me, 1); }

static void menu_cb_InsertOnAllTiers (FormantEditor me, EDITOR_ARGS_DIRECT) {
	const TextGrid grid =  my masterSlave.get();
	const integer saveTier = my selectedTier;
	for (integer itier = 1; itier <= grid -> tiers->size; itier ++)
		do_insertOnTier (me, itier);
	my selectedTier = saveTier;   // only if everything went right; otherwise, the tier where something went wrong will stand selected
}

/***** SEARCH MENU *****/

static void findInTier (FormantEditor me) {
	const TextGrid grid =  my masterSlave.get();
	checkTierSelection (me, U"find a text");
	Function anyTier = grid -> tiers->at [my selectedTier];
	if (anyTier -> classInfo == classIntervalTier) {
		const IntervalTier tier = (IntervalTier) anyTier;
		integer iinterval = IntervalTier_timeToIndex (tier, my startSelection) + 1;
		while (iinterval <= tier -> intervals.size) {
			TextInterval interval = tier -> intervals.at [iinterval];
			conststring32 text = interval -> text.get();
			if (text) {
				const char32 *position = str32str (text, my findString.get());
				if (position) {
					my startSelection = interval -> xmin;
					my endSelection = interval -> xmax;
					scrollToView (me, my startSelection);
					GuiText_setSelection (my text, position - text, position - text + str32len (my findString.get()));
					return;
				}
			}
			iinterval ++;
		}
		if (iinterval > tier -> intervals.size)
			Melder_beep ();
	} else {
		TextTier tier = (TextTier) anyTier;
		integer ipoint = AnyTier_timeToLowIndex (tier->asAnyTier(), my startSelection) + 1;
		while (ipoint <= tier -> points.size) {
			const TextPoint point = tier->points.at [ipoint];
			conststring32 text = point -> mark.get();
			if (text) {
				const char32 * const position = str32str (text, my findString.get());
				if (position) {
					my startSelection = my endSelection = point -> number;
					scrollToView (me, point -> number);
					GuiText_setSelection (my text, position - text, position - text + str32len (my findString.get()));
					return;
				}
			}
			ipoint ++;
		}
		if (ipoint > tier -> points.size)
			Melder_beep ();
	}
}

static void do_find (FormantEditor me) {
	if (my findString) {
		integer left, right;
		autostring32 label = GuiText_getStringAndSelectionPosition (my text, & left, & right);
		const char32 * const position = str32str (& label [right], my findString.get());   // CRLF BUG?
		if (position) {
			GuiText_setSelection (my text, position - label.get(), position - label.get() + str32len (my findString.get()));
		} else {
			findInTier (me);
		}
	}
}

static void menu_cb_Find (FormantEditor me, EDITOR_ARGS_FORM) {
	EDITOR_FORM (U"Find text", nullptr)
		TEXTFIELD (findString, U"Text:", U"")
	EDITOR_OK
	EDITOR_DO
		my findString = Melder_dup (findString);
		do_find (me);
	EDITOR_END
}

static void menu_cb_FindAgain (FormantEditor me, EDITOR_ARGS_DIRECT) {
	do_find (me);
}

static void checkSpellingInTier (FormantEditor me) {

}

static void menu_cb_CheckSpelling (FormantEditor me, EDITOR_ARGS_DIRECT) {
}

static void menu_cb_CheckSpellingInInterval (FormantEditor me, EDITOR_ARGS_DIRECT) {
}

static void menu_cb_AddToUserDictionary (FormantEditor me, EDITOR_ARGS_DIRECT) {
}

/***** TIER MENU *****/

static void menu_cb_RenameTier (FormantEditor me, EDITOR_ARGS_FORM) {
	EDITOR_FORM (U"Rename tier", nullptr)
		SENTENCE (newName, U"New name", U"");
	EDITOR_OK
		const TextGrid grid =  my masterSlave.get();
		checkTierSelection (me, U"rename a tier");
		const Daata tier = grid -> tiers->at [my selectedTier];
		SET_STRING (newName, tier -> name ? tier -> name.get() : U"")
	EDITOR_DO
		const TextGrid grid =  my masterSlave.get();
		checkTierSelection (me, U"rename a tier");
		const Function tier = grid -> tiers->at [my selectedTier];

		Editor_save (me, U"Rename tier");

		Thing_setName (tier, newName);

		FunctionEditor_redraw (me);
		Editor_broadcastDataChanged (me);
	EDITOR_END
}

static void menu_cb_PublishTier (FormantEditor me, EDITOR_ARGS_DIRECT) {
	const TextGrid grid =  my masterSlave.get();
	checkTierSelection (me, U"publish a tier");
	const Function tier = grid -> tiers->at [my selectedTier];
	autoTextGrid publish = TextGrid_createWithoutTiers (1e30, -1e30);
	TextGrid_addTier_copy (publish.get(), tier);
	Thing_setName (publish.get(), tier -> name.get());
	Editor_broadcastPublication (me, publish.move());
}

static void menu_cb_modeler_modelSettings (FormantEditor me, EDITOR_ARGS_FORM) {
	EDITOR_FORM (U"Formant modeler settings", nullptr)		
		SENTENCE (parameters_string, U"Number of parameters per track", my default_modeler_numberOfParametersPerTrack ())
		POSITIVE (varianceExponent, U"Variance exponent", U"1.25")
	EDITOR_OK
		SET_STRING (parameters_string, my p_modeler_numberOfParametersPerTrack)
	EDITOR_DO
	double startTime = my formantModelerList -> xmin, endTime = my formantModelerList -> xmax;
	my formantModelerList = FormantListWithHistory_to_FormantModelerList (my formantListWithHistory.get(), startTime, endTime, parameters_string);
	FormantModelerList_setVarianceExponent (my formantModelerList.get(), varianceExponent);
	pref_str32cpy2 (my pref_modeler_numberOfParametersPerTrack (), my p_modeler_numberOfParametersPerTrack, parameters_string);
	my pref_modeler_varianceExponent () = my p_modeler_varianceExponent = varianceExponent;
	my v_drawSelectionViewer ();
	EDITOR_END
}

static void menu_cb_modeler_modelSettingsDrawBest3 (FormantEditor me, EDITOR_ARGS_DIRECT) {
	my pref_modeler_draw_allModels () = my p_modeler_draw_allModels = false;
	autoINTVEC best3 = FormantModelerList_selectBest3 (my formantModelerList.get());
	my formantModelerList -> selected.part (1,3) <<= best3.get();
	my formantModelerList -> selected.resize (3);
	my v_drawSelectionViewer ();
}

static void menu_cb_modelerDrawingSettings (FormantEditor me, EDITOR_ARGS_FORM) {
	EDITOR_FORM (U"Formant modeler drawing settings", nullptr)
		BOOLEAN (drawAllModels, U"Draw all models", my default_modeler_draw_allModels ())
		BOOLEAN (drawEstimatedTracks, U"Draw estimated tracks", my default_modeler_draw_estimatedTracks ())
		REAL (xSpaceFraction, U"Column separation (fraction)", my default_modeler_draw_xSpace_fraction ())
		REAL (ySpaceFraction, U"Row separation (fraction)", my default_modeler_draw_ySpace_fraction ())
		POSITIVE (yGridLineEvery_Hz, U"Horizontal grid lines every (Hz)", my default_modeler_draw_yGridLineEvery_Hz ())
		POSITIVE (maximumFrequency, U"Maximum frequency (Hz)", my default_modeler_draw_maximumFrequency ())
		BOOLEAN (drawErrorBars, U"Draw error bars", my default_modeler_draw_errorBars ())
		REAL (errorBarWidth_s, U"Error bar width (s)", my default_modeler_draw_errorBarWidth_s ())
		REAL (xTrackShift_s, U"Shift even formant tracks by (s)", my default_modeler_draw_xTrackShift_s ())
	EDITOR_OK
		SET_BOOLEAN (drawAllModels, my p_modeler_draw_allModels)
		SET_BOOLEAN (drawEstimatedTracks, my p_modeler_draw_estimatedTracks)
		SET_REAL (xSpaceFraction, my p_modeler_draw_xSpace_fraction)
		SET_REAL (ySpaceFraction, my p_modeler_draw_ySpace_fraction)
		SET_REAL (yGridLineEvery_Hz, my p_modeler_draw_yGridLineEvery_Hz)
		SET_REAL (maximumFrequency, my p_modeler_draw_maximumFrequency)
		SET_BOOLEAN (drawErrorBars, my p_modeler_draw_errorBars)
		SET_REAL (errorBarWidth_s, my p_modeler_draw_errorBarWidth_s)
		SET_REAL (xTrackShift_s, my p_modeler_draw_xTrackShift_s)
	EDITOR_DO
	my pref_modeler_draw_allModels () = my p_modeler_draw_allModels = drawAllModels;
	my pref_modeler_draw_estimatedTracks () = my p_modeler_draw_estimatedTracks = drawEstimatedTracks;
	my pref_modeler_draw_xSpace_fraction () = my p_modeler_draw_xSpace_fraction = xSpaceFraction;
	my pref_modeler_draw_ySpace_fraction () = my p_modeler_draw_ySpace_fraction = ySpaceFraction;
	my pref_modeler_draw_maximumFrequency () = my p_modeler_draw_maximumFrequency = maximumFrequency;
	my pref_modeler_draw_yGridLineEvery_Hz () = my p_modeler_draw_yGridLineEvery_Hz = yGridLineEvery_Hz;
	my pref_modeler_draw_errorBars () = my p_modeler_draw_errorBars = drawErrorBars;
	my pref_modeler_draw_errorBarWidth_s () = my p_modeler_draw_errorBarWidth_s = errorBarWidth_s;
	my pref_modeler_draw_xTrackShift_s () = my p_modeler_draw_xTrackShift_s = xTrackShift_s;
	if (drawAllModels)
		FormantModelerList_selectAll (my formantModelerList.get());
	my v_drawSelectionViewer ();
	EDITOR_END
}

static void menu_cb_RemoveAllTextFromTier (FormantEditor me, EDITOR_ARGS_DIRECT) {
	const TextGrid grid =  my masterSlave.get();
	checkTierSelection (me, U"remove all text from a tier");
	IntervalTier intervalTier;
	TextTier textTier;
	_AnyTier_identifyClass (grid -> tiers->at [my selectedTier], & intervalTier, & textTier);

	Editor_save (me, U"Remove text from tier");
	if (intervalTier)
		IntervalTier_removeText (intervalTier);
	else
		TextTier_removeText (textTier);

	FunctionEditor_updateText (me);
	FunctionEditor_redraw (me);
	Editor_broadcastDataChanged (me);
}

static void menu_cb_RemoveTier (FormantEditor me, EDITOR_ARGS_DIRECT) {
	const TextGrid grid =  my masterSlave.get();
	if (grid -> tiers->size <= 1) {
		Melder_throw (U"Sorry, I refuse to remove the last tier.");
	}
	checkTierSelection (me, U"remove a tier");

	Editor_save (me, U"Remove tier");
	grid -> tiers-> removeItem (my selectedTier);

	my selectedTier = 1;
	FunctionEditor_updateText (me);
	FunctionEditor_redraw (me);
	Editor_broadcastDataChanged (me);
}

static void menu_cb_AddIntervalTier (FormantEditor me, EDITOR_ARGS_FORM) {
	EDITOR_FORM (U"Add interval tier", nullptr)
		NATURAL (position, U"Position", U"1 (= at top)")
		SENTENCE (name, U"Name", U"")
	EDITOR_OK
		const TextGrid grid =  my masterSlave.get();
		SET_INTEGER_AS_STRING (position, Melder_cat (grid -> tiers->size + 1, U" (= at bottom)"))
		SET_STRING (name, U"")
	EDITOR_DO
		const TextGrid grid =  my masterSlave.get();
		{// scope
			autoIntervalTier tier = IntervalTier_create (grid -> xmin, grid -> xmax);
			if (position > grid -> tiers->size)
				position = grid -> tiers->size + 1;
			Thing_setName (tier.get(), name);

			Editor_save (me, U"Add interval tier");
			grid -> tiers -> addItemAtPosition_move (tier.move(), position);
		}

		my selectedTier = position;
		FunctionEditor_updateText (me);
		FunctionEditor_redraw (me);
		Editor_broadcastDataChanged (me);
	EDITOR_END
}

static void menu_cb_DuplicateTier (FormantEditor me, EDITOR_ARGS_FORM) {
	EDITOR_FORM (U"Duplicate tier", nullptr)
		NATURAL (position, U"Position", U"1 (= at top)")
		SENTENCE (name, U"Name", U"")
	EDITOR_OK
		const TextGrid grid =  my masterSlave.get();
		if (my selectedTier) {
			SET_INTEGER (position, my selectedTier + 1)
			SET_STRING (name, grid -> tiers->at [my selectedTier] -> name.get())
		}
	EDITOR_DO
		const TextGrid grid =  my masterSlave.get();
		checkTierSelection (me, U"duplicate a tier");
		const Function tier = grid -> tiers->at [my selectedTier];
		{// scope
			autoFunction newTier = Data_copy (tier);
			if (position > grid -> tiers->size)
				position = grid -> tiers->size + 1;
			Thing_setName (newTier.get(), name);

			Editor_save (me, U"Duplicate tier");
			grid -> tiers -> addItemAtPosition_move (newTier.move(), position);
		}

		my selectedTier = position;
		FunctionEditor_updateText (me);
		FunctionEditor_redraw (me);
		Editor_broadcastDataChanged (me);
	EDITOR_END
}

/***** HELP MENU *****/

static void menu_cb_FormantEditorHelp (FormantEditor, EDITOR_ARGS_DIRECT) { Melder_help (U"FormantEditor"); }
static void menu_cb_AboutSpecialSymbols (FormantEditor, EDITOR_ARGS_DIRECT) { Melder_help (U"Special symbols"); }
static void menu_cb_PhoneticSymbols (FormantEditor, EDITOR_ARGS_DIRECT) { Melder_help (U"Phonetic symbols"); }
static void menu_cb_AboutTextStyles (FormantEditor, EDITOR_ARGS_DIRECT) { Melder_help (U"Text styles"); }

void structFormantEditor :: v_createMenus () {
	FormantEditor_Parent :: v_createMenus ();
	EditorMenu menu;

	#ifndef macintosh
		Editor_addCommand (this, U"Edit", U"-- cut copy paste --", 0, nullptr);
		Editor_addCommand (this, U"Edit", U"Cut text", 'X', menu_cb_Cut);
		Editor_addCommand (this, U"Edit", U"Cut", Editor_HIDDEN, menu_cb_Cut);
		Editor_addCommand (this, U"Edit", U"Copy text", 'C', menu_cb_Copy);
		Editor_addCommand (this, U"Edit", U"Copy", Editor_HIDDEN, menu_cb_Copy);
		Editor_addCommand (this, U"Edit", U"Paste text", 'V', menu_cb_Paste);
		Editor_addCommand (this, U"Edit", U"Paste", Editor_HIDDEN, menu_cb_Paste);
		Editor_addCommand (this, U"Edit", U"Erase text", 0, menu_cb_Erase);
		Editor_addCommand (this, U"Edit", U"Erase", Editor_HIDDEN, menu_cb_Erase);
	#endif
	Editor_addCommand (this, U"Edit", U"-- search --", 0, nullptr);
	Editor_addCommand (this, U"Edit", U"Find...", 'F', menu_cb_Find);
	Editor_addCommand (this, U"Edit", U"Find again", 'G', menu_cb_FindAgain);

	if (our d_sound.data) {
		Editor_addCommand (this, U"Select", U"-- move to zero --", 0, 0);
		Editor_addCommand (this, U"Select", U"Move start of selection to nearest zero crossing", ',', menu_cb_MoveBtoZero);
		Editor_addCommand (this, U"Select", U"Move begin of selection to nearest zero crossing", Editor_HIDDEN, menu_cb_MoveBtoZero);
		Editor_addCommand (this, U"Select", U"Move cursor to nearest zero crossing", '0', menu_cb_MoveCursorToZero);
		Editor_addCommand (this, U"Select", U"Move end of selection to nearest zero crossing", '.', menu_cb_MoveEtoZero);
	}

	Editor_addCommand (this, U"Query", U"-- query interval --", 0, nullptr);
	Editor_addCommand (this, U"Query", U"Get starting point of interval", 0, menu_cb_GetStartingPointOfInterval);
	Editor_addCommand (this, U"Query", U"Get end point of interval", 0, menu_cb_GetEndPointOfInterval);
	Editor_addCommand (this, U"Query", U"Get label of interval", 0, menu_cb_GetLabelOfInterval);

	menu = Editor_addMenu (this, U"Interval", 0);
	if (our d_sound.data || our d_longSound.data) {
		EditorMenu_addCommand (menu, U"Align interval", 'D', menu_cb_AlignInterval);
		EditorMenu_addCommand (menu, U"Alignment settings...", 0, menu_cb_AlignmentSettings);
		EditorMenu_addCommand (menu, U"-- add interval --", 0, nullptr);
	}
	EditorMenu_addCommand (menu, U"Add interval on slave tier", GuiMenu_COMMAND | '1', menu_cb_InsertIntervalOnSlaveTier);

	menu = Editor_addMenu (this, U"Boundary", 0);
	/*EditorMenu_addCommand (menu, U"Move to B", 0, menu_cb_MoveToB);
	EditorMenu_addCommand (menu, U"Move to E", 0, menu_cb_MoveToE);*/
	if (our d_sound.data) {
		EditorMenu_addCommand (menu, U"Move to nearest zero crossing", 0, menu_cb_MoveToZero);
		EditorMenu_addCommand (menu, U"-- insert boundary --", 0, nullptr);
	}
	EditorMenu_addCommand (menu, U"Add on selected tier", GuiMenu_ENTER, menu_cb_InsertOnSelectedTier);
	EditorMenu_addCommand (menu, U"Add on tier 1", GuiMenu_COMMAND | GuiMenu_F1, menu_cb_InsertOnTier1);
	EditorMenu_addCommand (menu, U"-- remove mark --", 0, nullptr);
	EditorMenu_addCommand (menu, U"Remove", GuiMenu_OPTION | GuiMenu_BACKSPACE, menu_cb_RemovePointOrBoundary);

	menu = Editor_addMenu (this, U"Tier", 0);
	EditorMenu_addCommand (menu, U"Add interval tier...", 0, menu_cb_AddIntervalTier);
	EditorMenu_addCommand (menu, U"Duplicate tier...", 0, menu_cb_DuplicateTier);
	EditorMenu_addCommand (menu, U"Rename tier...", 0, menu_cb_RenameTier);
	EditorMenu_addCommand (menu, U"-- remove tier --", 0, nullptr);
	EditorMenu_addCommand (menu, U"Remove all text from tier", 0, menu_cb_RemoveAllTextFromTier);
	EditorMenu_addCommand (menu, U"Remove entire tier", 0, menu_cb_RemoveTier);
	EditorMenu_addCommand (menu, U"-- extract tier --", 0, nullptr);
	EditorMenu_addCommand (menu, U"Extract to list of objects:", GuiMenu_INSENSITIVE, menu_cb_PublishTier /* dummy */);
	EditorMenu_addCommand (menu, U"Extract entire selected tier", 0, menu_cb_PublishTier);

	if (our d_sound.data || our d_longSound.data) {
		if (our v_hasAnalysis ())
			our v_createMenus_analysis ();   // insert some of the ancestor's menus *after* the TextGrid menus
	}
	menu = Editor_addMenu (this, U"Modeling", 0);
	EditorMenu_addCommand (menu, U"Model settings...", 0, menu_cb_modeler_modelSettings);
	EditorMenu_addCommand (menu, U"Draw best three", 0, menu_cb_modeler_modelSettingsDrawBest3);
	EditorMenu_addCommand (menu, U"Drawing settings...", 0, menu_cb_modelerDrawingSettings);
}

void structFormantEditor :: v_createHelpMenuItems (EditorMenu menu) {
	FormantEditor_Parent :: v_createHelpMenuItems (menu);
	EditorMenu_addCommand (menu, U"FormantEditor help", '?', menu_cb_FormantEditorHelp);
	EditorMenu_addCommand (menu, U"About special symbols", 0, menu_cb_AboutSpecialSymbols);
	EditorMenu_addCommand (menu, U"Phonetic symbols", 0, menu_cb_PhoneticSymbols);
	EditorMenu_addCommand (menu, U"About text styles", 0, menu_cb_AboutTextStyles);
}

/***** CHILDREN *****/

static void gui_text_cb_changed (FormantEditor me, GuiTextEvent /* event */) {
	const TextGrid grid =  my masterSlave.get();
	//Melder_casual (U"gui_text_cb_change 1 in editor ", Melder_pointer (me));
	if (my suppressRedraw) return;   /* Prevent infinite loop if 'draw' method or Editor_broadcastChange calls GuiText_setString. */
	//Melder_casual (U"gui_text_cb_change 2 in editor ", me);
	if (my selectedTier) {
		autostring32 text = GuiText_getString (my text);
		IntervalTier intervalTier;
		TextTier textTier;
		_AnyTier_identifyClass (grid -> tiers->at [my selectedTier], & intervalTier, & textTier);
		if (intervalTier) {
			const integer selectedInterval = getSelectedInterval (me);
			if (selectedInterval) {
				TextInterval interval = intervalTier -> intervals.at [selectedInterval];
				//Melder_casual (U"gui_text_cb_change 3 in editor ", Melder_pointer (me));
				TextInterval_setText (interval, text.get());
				//Melder_casual (U"gui_text_cb_change 4 in editor ", Melder_pointer (me));
				FunctionEditor_redraw (me);
				//Melder_casual (U"gui_text_cb_change 5 in editor ", Melder_pointer (me));
				Editor_broadcastDataChanged (me);
				//Melder_casual (U"gui_text_cb_change 6 in editor ", Melder_pointer (me));
			}
		} else {
			const integer selectedPoint = getSelectedPoint (me);
			if (selectedPoint) {
				TextPoint point = textTier -> points.at [selectedPoint];
				point -> mark. reset();
				if (Melder_findInk (text.get()))   // any visible characters?
					point -> mark = Melder_dup_f (text.get());
				FunctionEditor_redraw (me);
				Editor_broadcastDataChanged (me);
			}
		}
	}
}

void structFormantEditor :: v_createChildren () {
	FormantEditor_Parent :: v_createChildren ();
	if (our text)
		GuiText_setChangedCallback (our text, gui_text_cb_changed, this);
}

void structFormantEditor :: v_dataChanged () {
	const TextGrid grid = our masterSlave.get();
	/*
		Perform a minimal selection change.
		Most changes will involve intervals and boundaries; however, there may also be tier removals.
		Do a simple guess.
	*/
	if (our selectedTier > grid -> tiers->size)
		our selectedTier = grid -> tiers->size;
	FormantEditor_Parent :: v_dataChanged ();   // does all the updating
}

/********** DRAWING AREA **********/

void structFormantEditor :: v_prepareDraw () {
	if (our d_longSound.data) {
		try {
			LongSound_haveWindow (our d_longSound.data, our startWindow, our endWindow);
		} catch (MelderError) {
			Melder_clearError ();
		}
	}
}

static void do_drawIntervalTier (FormantEditor me, IntervalTier tier, integer itier) {
	#if gtk || defined (macintosh)
		constexpr bool platformUsesAntiAliasing = true;
	#else
		constexpr bool platformUsesAntiAliasing = false;
	#endif
	integer x1DC, x2DC, yDC;
	Graphics_WCtoDC (my graphics.get(), my startWindow, 0.0, & x1DC, & yDC);
	Graphics_WCtoDC (my graphics.get(), my endWindow, 0.0, & x2DC, & yDC);
	Graphics_setPercentSignIsItalic (my graphics.get(), my p_useTextStyles);
	Graphics_setNumberSignIsBold (my graphics.get(), my p_useTextStyles);
	Graphics_setCircumflexIsSuperscript (my graphics.get(), my p_useTextStyles);
	Graphics_setUnderscoreIsSubscript (my graphics.get(), my p_useTextStyles);

	/*
		Highlight interval: yellow (selected) or green (matching label).
	*/
	const integer selectedInterval = ( itier == my selectedTier ? getSelectedInterval (me) : 0 ), ninterval = tier -> intervals.size;
	for (integer iinterval = 1; iinterval <= ninterval; iinterval ++) {
		TextInterval interval = tier -> intervals.at [iinterval];
		double tmin = interval -> xmin, tmax = interval -> xmax;
		if (tmax > my startWindow && tmin < my endWindow) {   // interval visible?
			const bool intervalIsSelected = ( iinterval == selectedInterval );
			const bool labelDoesMatch = Melder_stringMatchesCriterion (interval -> text.get(), my p_greenMethod, my p_greenString, true);
			if (tmin < my startWindow)
				tmin = my startWindow;
			if (tmax > my endWindow)
				tmax = my endWindow;
			if (labelDoesMatch) {
				Graphics_setColour (my graphics.get(), Melder_LIME);
				Graphics_fillRectangle (my graphics.get(), tmin, tmax, 0.0, 1.0);
			}
			if (intervalIsSelected) {
				if (labelDoesMatch) {
					tmin = 0.85 * tmin + 0.15 * tmax;
					tmax = 0.15 * tmin + 0.85 * tmax;
				}
				Graphics_setColour (my graphics.get(), Melder_YELLOW);
				Graphics_fillRectangle (my graphics.get(), tmin, tmax,
						labelDoesMatch ? 0.15 : 0.0, labelDoesMatch? 0.85: 1.0);
			}
		}
	}
	Graphics_setColour (my graphics.get(), Melder_BLACK);
	Graphics_line (my graphics.get(), my endWindow, 0.0, my endWindow, 1.0);

	/*
	 * Draw a grey bar and a selection button at the cursor position.
	 */
	if (my startSelection == my endSelection && my startSelection >= my startWindow && my startSelection <= my endWindow) {
		bool cursorAtBoundary = false;
		for (integer iinterval = 2; iinterval <= ninterval; iinterval ++) {
			const TextInterval interval = tier -> intervals.at [iinterval];
			if (interval -> xmin == my startSelection)
				cursorAtBoundary = true;
		}
		if (! cursorAtBoundary) {
			const double dy = Graphics_dyMMtoWC (my graphics.get(), 1.5);
			Graphics_setGrey (my graphics.get(), 0.8);
			Graphics_setLineWidth (my graphics.get(), platformUsesAntiAliasing ? 6.0 : 5.0);
			Graphics_line (my graphics.get(), my startSelection, 0.0, my startSelection, 1.0);
			Graphics_setLineWidth (my graphics.get(), 1.0);
			Graphics_setColour (my graphics.get(), Melder_BLUE);
			Graphics_circle_mm (my graphics.get(), my startSelection, 1.0 - dy, 3.0);
		}
	}

	Graphics_setTextAlignment (my graphics.get(), my p_alignment, Graphics_HALF);
	for (integer iinterval = 1; iinterval <= ninterval; iinterval ++) {
		const TextInterval interval = tier -> intervals.at [iinterval];
		double tmin = interval -> xmin, tmax = interval -> xmax;
		if (tmin < my tmin)
			tmin = my tmin;
		if (tmax > my tmax)
			tmax = my tmax;
		if (tmin >= tmax)
			continue;
		const bool intervalIsSelected = ( selectedInterval == iinterval );

		/*
			Draw left boundary.
		*/
		if (tmin >= my startWindow && tmin <= my endWindow && iinterval > 1) {
			const bool boundaryIsSelected = ( my selectedTier == itier && tmin == my startSelection );
			Graphics_setColour (my graphics.get(), boundaryIsSelected ? Melder_RED : Melder_BLUE);
			Graphics_setLineWidth (my graphics.get(), platformUsesAntiAliasing ? 6.0 : 5.0);
			Graphics_line (my graphics.get(), tmin, 0.0, tmin, 1.0);

			/*
				Show alignment with cursor.
			*/
			if (tmin == my startSelection) {
				Graphics_setColour (my graphics.get(), Melder_YELLOW);
				Graphics_setLineWidth (my graphics.get(), platformUsesAntiAliasing ? 2.0 : 1.0);
				Graphics_line (my graphics.get(), tmin, 0.0, tmin, 1.0);
			}
		}
		Graphics_setLineWidth (my graphics.get(), 1.0);

		/*
			Draw label text.
		*/
		if (interval -> text && tmax >= my startWindow && tmin <= my endWindow) {
			const double t1 = std::max (my startWindow, tmin);
			const double t2 = std::min (my endWindow, tmax);
			Graphics_setColour (my graphics.get(), intervalIsSelected ? Melder_RED : Melder_BLACK);
			Graphics_textRect (my graphics.get(), t1, t2, 0.0, 1.0, interval -> text.get());
			Graphics_setColour (my graphics.get(), Melder_BLACK);
		}

	}
	Graphics_setPercentSignIsItalic (my graphics.get(), true);
	Graphics_setNumberSignIsBold (my graphics.get(), true);
	Graphics_setCircumflexIsSuperscript (my graphics.get(), true);
	Graphics_setUnderscoreIsSubscript (my graphics.get(), true);
}

static void do_drawTextTier (FormantEditor me, TextTier tier, integer itier) {
	#if gtk || defined (macintosh)
		constexpr bool platformUsesAntiAliasing = true;
	#else
		constexpr bool platformUsesAntiAliasing = false;
	#endif
	const integer npoint = tier -> points.size;
	Graphics_setPercentSignIsItalic (my graphics.get(), my p_useTextStyles);
	Graphics_setNumberSignIsBold (my graphics.get(), my p_useTextStyles);
	Graphics_setCircumflexIsSuperscript (my graphics.get(), my p_useTextStyles);
	Graphics_setUnderscoreIsSubscript (my graphics.get(), my p_useTextStyles);

	/*
	 * Draw a grey bar and a selection button at the cursor position.
	 */
	if (my startSelection == my endSelection && my startSelection >= my startWindow && my startSelection <= my endWindow) {
		bool cursorAtPoint = false;
		for (integer ipoint = 1; ipoint <= npoint; ipoint ++) {
			const TextPoint point = tier -> points.at [ipoint];
			if (point -> number == my startSelection)
				cursorAtPoint = true;
		}
		if (! cursorAtPoint) {
			const double dy = Graphics_dyMMtoWC (my graphics.get(), 1.5);
			Graphics_setGrey (my graphics.get(), 0.8);
			Graphics_setLineWidth (my graphics.get(), platformUsesAntiAliasing ? 6.0 : 5.0);
			Graphics_line (my graphics.get(), my startSelection, 0.0, my startSelection, 1.0);
			Graphics_setLineWidth (my graphics.get(), 1.0);
			Graphics_setColour (my graphics.get(), Melder_BLUE);
			Graphics_circle_mm (my graphics.get(), my startSelection, 1.0 - dy, 3.0);
		}
	}

	Graphics_setTextAlignment (my graphics.get(), Graphics_CENTRE, Graphics_HALF);
	for (integer ipoint = 1; ipoint <= npoint; ipoint ++) {
		const TextPoint point = tier -> points.at [ipoint];
		const double t = point -> number;
		if (t >= my startWindow && t <= my endWindow) {
			const bool pointIsSelected = ( itier == my selectedTier && t == my startSelection );
			Graphics_setColour (my graphics.get(), pointIsSelected ? Melder_RED : Melder_BLUE);
			Graphics_setLineWidth (my graphics.get(), platformUsesAntiAliasing ? 6.0 : 5.0);
			Graphics_line (my graphics.get(), t, 0.0, t, 0.2);
			Graphics_line (my graphics.get(), t, 0.8, t, 1);
			Graphics_setLineWidth (my graphics.get(), 1.0);

			/*
				Wipe out the cursor where the text is going to be.
			*/
			Graphics_setColour (my graphics.get(), Melder_WHITE);
			Graphics_line (my graphics.get(), t, 0.2, t, 0.8);

			/*
				Show alignment with cursor.
			*/
			if (my startSelection == my endSelection && t == my startSelection) {
				Graphics_setColour (my graphics.get(), Melder_YELLOW);
				Graphics_setLineWidth (my graphics.get(), platformUsesAntiAliasing ? 2.0 : 1.0);
				Graphics_line (my graphics.get(), t, 0.0, t, 0.2);
				Graphics_line (my graphics.get(), t, 0.8, t, 1.0);
				Graphics_setLineWidth (my graphics.get(), 1.0);
			}
			Graphics_setColour (my graphics.get(), pointIsSelected ? Melder_RED : Melder_BLUE);
			if (point -> mark)
				Graphics_text (my graphics.get(), t, 0.5, point -> mark.get());
		}
	}
	Graphics_setPercentSignIsItalic (my graphics.get(), true);
	Graphics_setNumberSignIsBold (my graphics.get(), true);
	Graphics_setCircumflexIsSuperscript (my graphics.get(), true);
	Graphics_setUnderscoreIsSubscript (my graphics.get(), true);
}

void structFormantEditor :: v_draw () {
	const TextGrid grid = our masterSlave.get();
	Graphics_Viewport vp1, vp2;
	const integer ntier = grid -> tiers->size;
	const enum kGraphics_font oldFont = Graphics_inqFont (our graphics.get());
	const double oldFontSize = Graphics_inqFontSize (our graphics.get());
	const bool showAnalysis = v_hasAnalysis () &&
			(p_spectrogram_show || p_pitch_show || p_intensity_show || p_formant_show) &&
			(d_longSound.data || d_sound.data);
	const double soundY = _FormantEditor_computeSoundY (this), soundY2 = showAnalysis ? 0.5 * (1.0 + soundY) : soundY;

	/*
		Draw optional sound.
	*/
	if (d_longSound.data || d_sound.data) {
		vp1 = Graphics_insetViewport (our graphics.get(), 0.0, 1.0, soundY2, 1.0);
		Graphics_setColour (our graphics.get(), Melder_WHITE);
		Graphics_setWindow (our graphics.get(), 0.0, 1.0, 0.0, 1.0);
		Graphics_fillRectangle (our graphics.get(), 0.0, 1.0, 0.0, 1.0);
		TimeSoundEditor_drawSound (this, -1.0, 1.0);
		//Graphics_flushWs (our graphics.get());
		Graphics_resetViewport (our graphics.get(), vp1);
	}

	/*
		Draw tiers.
	*/
	if (d_longSound.data || d_sound.data) vp1 = Graphics_insetViewport (our graphics.get(), 0.0, 1.0, 0.0, soundY);
	Graphics_setColour (our graphics.get(), Melder_WHITE);
	Graphics_setWindow (our graphics.get(), 0.0, 1.0, 0.0, 1.0);
	Graphics_fillRectangle (our graphics.get(), 0.0, 1.0, 0.0, 1.0);
	Graphics_setColour (our graphics.get(), Melder_BLACK);
	Graphics_rectangle (our graphics.get(), 0.0, 1.0, 0.0, 1.0);
	Graphics_setWindow (our graphics.get(), our startWindow, our endWindow, 0.0, 1.0);
	for (integer itier = 1; itier <= ntier; itier ++) {
		const Function anyTier = grid -> tiers->at [itier];
		const bool tierIsSelected = ( itier == selectedTier );
		const bool isIntervalTier = ( anyTier -> classInfo == classIntervalTier );
		vp2 = Graphics_insetViewport (our graphics.get(), 0.0, 1.0,
			1.0 - (double) itier / (double) ntier,
			1.0 - (double) (itier - 1) / (double) ntier);
		Graphics_setColour (our graphics.get(), Melder_BLACK);
		if (itier != 1)
			Graphics_line (our graphics.get(), our startWindow, 1.0, our endWindow, 1.0);

		/*
			Show the number and the name of the tier.
		*/
		Graphics_setColour (our graphics.get(), tierIsSelected ? Melder_RED : Melder_BLACK);
		Graphics_setFont (our graphics.get(), oldFont);
		Graphics_setFontSize (our graphics.get(), 14);
		Graphics_setTextAlignment (our graphics.get(), Graphics_RIGHT, Graphics_HALF);
		Graphics_text (our graphics.get(), our startWindow, 0.5,   tierIsSelected ? U"☞ " : U"", itier);
		Graphics_setFontSize (our graphics.get(), oldFontSize);
		if (anyTier -> name && anyTier -> name [0]) {
			Graphics_setTextAlignment (our graphics.get(), Graphics_LEFT,
				our p_showNumberOf == kTextGridEditor_showNumberOf::NOTHING ? Graphics_HALF : Graphics_BOTTOM);
			Graphics_text (our graphics.get(), our endWindow, 0.5, anyTier -> name.get());
		}
		if (our p_showNumberOf != kTextGridEditor_showNumberOf::NOTHING) {
			Graphics_setTextAlignment (our graphics.get(), Graphics_LEFT, Graphics_TOP);
			if (our p_showNumberOf == kTextGridEditor_showNumberOf::INTERVALS_OR_POINTS) {
				integer count = isIntervalTier ? ((IntervalTier) anyTier) -> intervals.size : ((TextTier) anyTier) -> points.size;
				integer position = itier == selectedTier ? ( isIntervalTier ? getSelectedInterval (this) : getSelectedPoint (this) ) : 0;
				if (position)
					Graphics_text (our graphics.get(), our endWindow, 0.5,   U"(", position, U"/", count, U")");
				else
					Graphics_text (our graphics.get(), our endWindow, 0.5,   U"(", count, U")");
			} else {
				Melder_assert (our p_showNumberOf == kTextGridEditor_showNumberOf::NONEMPTY_INTERVALS_OR_POINTS);
				integer count = 0;
				if (isIntervalTier) {
					const IntervalTier tier = (IntervalTier) anyTier;
					const integer numberOfIntervals = tier -> intervals.size;
					for (integer iinterval = 1; iinterval <= numberOfIntervals; iinterval ++) {
						const TextInterval interval = tier -> intervals.at [iinterval];
						if (interval -> text && interval -> text [0] != U'\0')
							count ++;
					}
				} else {
					const TextTier tier = (TextTier) anyTier;
					const integer numberOfPoints = tier -> points.size;
					for (integer ipoint = 1; ipoint <= numberOfPoints; ipoint ++) {
						const TextPoint point = tier -> points.at [ipoint];
						if (point -> mark && point -> mark [0] != U'\0')
							count ++;
					}
				}
				Graphics_text (our graphics.get(), our endWindow, 0.5,   U"(##", count, U"#)");
			}
		}

		Graphics_setColour (our graphics.get(), Melder_BLACK);
		Graphics_setFont (our graphics.get(), kGraphics_font::TIMES);
		Graphics_setFontSize (our graphics.get(), p_fontSize);
		if (isIntervalTier)
			do_drawIntervalTier (this, (IntervalTier) anyTier, itier);
		else
			do_drawTextTier (this, (TextTier) anyTier, itier);
		Graphics_resetViewport (our graphics.get(), vp2);
	}
	Graphics_setColour (our graphics.get(), Melder_BLACK);
	Graphics_setFont (our graphics.get(), oldFont);
	Graphics_setFontSize (our graphics.get(), oldFontSize);
	if (d_longSound.data || d_sound.data)
		Graphics_resetViewport (our graphics.get(), vp1);
	//Graphics_flushWs (our graphics.get());

	if (showAnalysis) {
		vp1 = Graphics_insetViewport (our graphics.get(), 0.0, 1.0, soundY, soundY2);
		v_draw_analysis ();
		//Graphics_flushWs (our graphics.get());
		Graphics_resetViewport (our graphics.get(), vp1);
		/* Draw pulses. */
		if (p_pulses_show) {
			vp1 = Graphics_insetViewport (our graphics.get(), 0.0, 1.0, soundY2, 1.0);
			v_draw_analysis_pulses ();
			TimeSoundEditor_drawSound (this, -1.0, 1.0);   // second time, partially across the pulses
			//Graphics_flushWs (our graphics.get());
			Graphics_resetViewport (our graphics.get(), vp1);
		}
	}
	Graphics_setWindow (our graphics.get(), our startWindow, our endWindow, 0.0, 1.0);
	if (our d_longSound.data || our d_sound.data) {
		Graphics_line (our graphics.get(), our startWindow, soundY, our endWindow, soundY);
		if (showAnalysis) {
			Graphics_line (our graphics.get(), our startWindow, soundY2, our endWindow, soundY2);
			Graphics_line (our graphics.get(), our startWindow, soundY, our startWindow, soundY2);
			Graphics_line (our graphics.get(), our endWindow, soundY, our endWindow, soundY2);
		}
	}

	/*
		Finally, us usual, update the menus.
	*/
	v_updateMenuItems_file ();
}

void FormantEditor_setSelectionViewerViewportAndWindow (FormantEditor me) {
	/*
		BOTTOM_MARGIN = 2; TOP_MARGIN = 3; MARGIN = 107; space = 30
		The FunctionEditor defines the selectionViewer viewport as
		Graphics_setViewport (my graphics.get(), my selectionViewerLeft + MARGIN, my selectionViewerRight - MARGIN, BOTTOM_MARGIN + space * 3, my height - (TOP_MARGIN + space));
		my v_drawSelectionViewer ();
		We need somewhat more space; idealy we could override the values above, for now they are hard-coded
	*/
	double space = 30.0, margin = 107.0;
	double vp_left = my selectionViewerLeft + 0.5 * margin ;
	double vp_right = my selectionViewerRight - 0.75 * margin;
	double vp_bottom = space;
	double vp_top = my height - space;
	Graphics_setViewport (my graphics.get(), vp_left, vp_right, vp_bottom, vp_top);
	Graphics_setWindow (my graphics.get(), vp_left, vp_right, vp_bottom, vp_top);
}

void structFormantEditor :: v_drawSelectionViewer () {
	/*
		BOTTOM_MARGIN = 2; TOP_MARGIN = 3; MARGIN = 107; space = 30
		The FunctionEditor defines the selectionViewer viewport as
		Graphics_setViewport (my graphics.get(), my selectionViewerLeft + MARGIN, my selectionViewerRight - MARGIN, BOTTOM_MARGIN + space * 3, my height - (TOP_MARGIN + space));
		my v_drawSelectionViewer ();
		We need somewhat more space; idealy we could override the values above, for now they are hard-coded
	*/
	double space = 30.0, margin = 107.0;
	double vp_left = selectionViewerLeft + 0.5 * margin ;
	double vp_right = selectionViewerRight - 0.75 * margin;
	double vp_bottom = space;
	double vp_top = height - space;
	Graphics_setViewport (our graphics.get(), vp_left, vp_right, vp_bottom, vp_top);
	Graphics_setWindow (our graphics.get(), vp_left, vp_right, vp_bottom, vp_top);
	Graphics_setColour (our graphics.get(), Melder_WHITE);
	Graphics_fillRectangle (our graphics.get(), vp_left, vp_right, vp_bottom, vp_top);
	Graphics_setColour (our graphics.get(), Melder_BLACK);
	Graphics_setFont (our graphics.get(), kGraphics_font::TIMES);
	Graphics_setFontSize (our graphics.get(), 9.0);
	Graphics_setTextAlignment (our graphics.get(), Graphics_CENTRE, Graphics_HALF);
	if (! our formantModelerList.get())
			return;
	double startTime = startWindow, endTime = endWindow;
	if (startSelection < endSelection) {
		startTime = startSelection;
		endTime = endSelection;
	}
	if (startSelection < endSelection && (startTime != our formantModelerList -> xmin || endTime != our formantModelerList -> xmax))
		our formantModelerList = FormantListWithHistory_to_FormantModelerList (our formantListWithHistory.get(), startTime, endTime, our p_modeler_numberOfParametersPerTrack);

	FormantModelerList fml = our formantModelerList.get();
	FormantModelerList_setVarianceExponent (fml, our p_modeler_varianceExponent);
	const double xCursor = ( startSelection == endSelection ? startSelection : fml -> xmin - 10.0 );
	const double yCursor = ( our d_spectrogram_cursor > our p_spectrogram_viewFrom && our d_spectrogram_cursor < our p_spectrogram_viewTo ? our d_spectrogram_cursor : -1000.0 );
	FormantModelerList_drawAsMatrix (fml, our graphics.get(), 0, 0, kGraphicsMatrixOrigin::TOP_LEFT,
		our p_modeler_draw_xSpace_fraction, our p_modeler_draw_ySpace_fraction, 1,
		our formantModelerList -> numberOfTracksPerModel, our p_modeler_draw_maximumFrequency,
		our p_modeler_draw_yGridLineEvery_Hz, xCursor, yCursor, 0, our p_modeler_draw_errorBars,
		our p_modeler_draw_errorBarWidth_s, our p_modeler_draw_xTrackShift_s,
		our p_modeler_draw_estimatedTracks, true);
}

static void do_drawWhileDragging (FormantEditor me, double numberOfTiers, bool selectedTier [], double x, double soundY) {
	for (integer itier = 1; itier <= numberOfTiers; itier ++) {
		if (selectedTier [itier]) {
			const double ymin = soundY * (1.0 - (double) itier / numberOfTiers);
			const double ymax = soundY * (1.0 - (double) (itier - 1) / numberOfTiers);
			Graphics_setLineWidth (my graphics.get(), 7.0);
			Graphics_line (my graphics.get(), x, ymin, x, ymax);
		}
	}
	Graphics_setLineWidth (my graphics.get(), 1);
	Graphics_line (my graphics.get(), x, 0.0, x, 1.01);
	Graphics_text (my graphics.get(), x, 1.01, Melder_fixed (x, 6));
}

static void do_dragBoundary (FormantEditor me, double xbegin, integer iClickedTier, int shiftKeyPressed) {
	const TextGrid grid =  my masterSlave.get();
	const integer numberOfTiers = grid -> tiers->size;
	double xWC = xbegin, yWC;
	double leftDraggingBoundary = my tmin, rightDraggingBoundary = my tmax;   // initial dragging range
	bool selectedTier [1000];
	const double soundY = _FormantEditor_computeSoundY (me);

	/*
		Determine the set of selected boundaries and points, and the dragging range.
	*/
	for (int itier = 1; itier <= numberOfTiers; itier ++) {
		selectedTier [itier] = false;   // the default
		/*
			If the user has pressed the shift key, let her drag all the boundaries and points at this time.
			Otherwise, let her only drag the boundary or point on the clicked tier.
		*/
		if (itier == iClickedTier || shiftKeyPressed == my p_shiftDragMultiple) {
			IntervalTier intervalTier;
			TextTier textTier;
			_AnyTier_identifyClass (grid -> tiers->at [itier], & intervalTier, & textTier);
			if (intervalTier) {
				integer ibound = IntervalTier_hasBoundary (intervalTier, xbegin);
				if (ibound) {
					TextInterval leftInterval = intervalTier -> intervals.at [ibound - 1];
					TextInterval rightInterval = intervalTier -> intervals.at [ibound];
					selectedTier [itier] = true;
					/*
						Prevent the user from dragging the boundary past its left or right neighbours on the same tier.
					*/
					if (leftInterval -> xmin > leftDraggingBoundary)
						leftDraggingBoundary = leftInterval -> xmin;
					if (rightInterval -> xmax < rightDraggingBoundary)
						rightDraggingBoundary = rightInterval -> xmax;
				}
			} else {
				if (AnyTier_hasPoint (textTier->asAnyTier(), xbegin)) {
					/*
						Other than with boundaries on interval tiers,
						points on text tiers can be dragged past their neighbours.
					*/
					selectedTier [itier] = true;
				}
			}
		}
	}

	Graphics_xorOn (my graphics.get(), Melder_MAROON);
	Graphics_setTextAlignment (my graphics.get(), Graphics_CENTRE, Graphics_BOTTOM);
	do_drawWhileDragging (me, numberOfTiers, selectedTier, xWC, soundY);   // draw at old position
	while (Graphics_mouseStillDown (my graphics.get())) {
		double xWC_new;
		Graphics_getMouseLocation (my graphics.get(), & xWC_new, & yWC);
		if (xWC_new != xWC) {
			do_drawWhileDragging (me, numberOfTiers, selectedTier, xWC, soundY);   // undraw at old position
			xWC = xWC_new;
			do_drawWhileDragging (me, numberOfTiers, selectedTier, xWC, soundY);   // draw at new position
		}
	}
	do_drawWhileDragging (me, numberOfTiers, selectedTier, xWC, soundY);   // undraw at new position
	Graphics_xorOff (my graphics.get());

	/*
		The simplest way to cancel the dragging operation, is to drag outside the window.
	*/
	if (xWC <= my startWindow || xWC >= my endWindow)
		return;

	/*
		If the user dropped near an existing boundary in an unselected tier or near the cursor,
		we snap to that mark.
	*/
	const integer itierDrop = _FormantEditor_yWCtoTier (me, yWC);
	if (yWC > 0.0 && yWC < soundY && ! selectedTier [itierDrop]) {   // dropped inside an unselected tier?
		const Function anyTierDrop = grid -> tiers->at [itierDrop];
		if (anyTierDrop -> classInfo == classIntervalTier) {
			const IntervalTier tierDrop = (IntervalTier) anyTierDrop;
			for (integer ibound = 1; ibound < tierDrop -> intervals.size; ibound ++) {
				const TextInterval left = tierDrop -> intervals.at [ibound];
				if (fabs (Graphics_dxWCtoMM (my graphics.get(), xWC - left -> xmax)) < 1.5) {   // near a boundary?
					/*
						Snap to boundary.
					*/
					xWC = left -> xmax;
				}
			}
		} else {
			const TextTier tierDrop = (TextTier) anyTierDrop;
			for (integer ipoint = 1; ipoint <= tierDrop -> points.size; ipoint ++) {
				TextPoint point = tierDrop -> points.at [ipoint];
				if (fabs (Graphics_dxWCtoMM (my graphics.get(), xWC - point -> number)) < 1.5) {   // near a point?
					/*
						Snap to point.
					*/
					xWC = point -> number;
				}
			}
		}
	} else if (xbegin != my startSelection && fabs (Graphics_dxWCtoMM (my graphics.get(), xWC - my startSelection)) < 1.5) {   // near the cursor?
		/*
			Snap to cursor.
		*/
		xWC = my startSelection;
	} else if (xbegin != my endSelection && fabs (Graphics_dxWCtoMM (my graphics.get(), xWC - my endSelection)) < 1.5) {   // near the cursor?
		/*
			Snap to cursor.
		*/
		xWC = my endSelection;
	}

	/*
		We cannot move a boundary out of the dragging range.
	*/
	if (xWC <= leftDraggingBoundary || xWC >= rightDraggingBoundary) {
		Melder_beep ();
		return;
	}

	Editor_save (me, U"Drag");

	for (integer itier = 1; itier <= numberOfTiers; itier ++) {
		if (selectedTier [itier]) {
			IntervalTier intervalTier;
			TextTier textTier;
			_AnyTier_identifyClass (grid -> tiers->at [itier], & intervalTier, & textTier);
			if (intervalTier) {
				const integer numberOfIntervals = intervalTier -> intervals.size;
				for (integer ibound = 2; ibound <= numberOfIntervals; ibound ++) {
					TextInterval left = intervalTier -> intervals.at [ibound - 1], right = intervalTier -> intervals.at [ibound];
					if (left -> xmax == xbegin) {   // boundary dragged?
						left -> xmax = right -> xmin = xWC;   // move boundary to drop site
						break;
					}
				}
			} else {
				const integer iDraggedPoint = AnyTier_hasPoint (textTier->asAnyTier(), xbegin);
				if (iDraggedPoint) {
					integer dropSiteHasPoint = AnyTier_hasPoint (textTier->asAnyTier(), xWC);
					if (dropSiteHasPoint != 0) {
						Melder_warning (U"Cannot drop point on an existing point.");
					} else {
						const TextPoint point = textTier -> points.at [iDraggedPoint];
						/*
							Move point to drop site. May have passed another point.
						*/
						autoTextPoint newPoint = Data_copy (point);
						newPoint -> number = xWC;   // move point to drop site
						textTier -> points. removeItem (iDraggedPoint);
						textTier -> points. addItem_move (newPoint.move());
					}
				}
			}
		}
	}

	/*
		Select the drop site.
	*/
	if (my startSelection == xbegin)
		my startSelection = xWC;
	if (my endSelection == xbegin)
		my endSelection = xWC;
	if (my startSelection > my endSelection) {
		double dummy = my startSelection;
		my startSelection = my endSelection;
		my endSelection = dummy;
	}
	FunctionEditor_marksChanged (me, true);
	Editor_broadcastDataChanged (me);
}

bool structFormantEditor :: v_click (double xclick, double yWC, bool shiftKeyPressed) {
	const TextGrid grid = our masterSlave.get();

	/*
		In answer to a click in the sound part,
		we keep the same tier selected and move the cursor or drag the "yellow" selection.
	*/
	const double soundY = _FormantEditor_computeSoundY (this);
	if (yWC > soundY) {   // clicked in sound part?
		if ((our p_spectrogram_show || our p_formant_show) && yWC < 0.5 * (soundY + 1.0)) {
			our d_spectrogram_cursor = our p_spectrogram_viewFrom +
					2.0 * (yWC - soundY) / (1.0 - soundY) * (our p_spectrogram_viewTo - our p_spectrogram_viewFrom);
		}
		our FormantEditor_Parent :: v_click (xclick, yWC, shiftKeyPressed);
		return FunctionEditor_UPDATE_NEEDED;
	}

	/*
		The user clicked in the grid part.
		We select the tier in which she clicked.
	*/
	const integer clickedTierNumber = _FormantEditor_yWCtoTier (this, yWC);

	if (xclick <= our startWindow || xclick >= our endWindow) {
		our selectedTier = clickedTierNumber;
		return FunctionEditor_UPDATE_NEEDED;
	}

	double tmin, tmax;
	_FormantEditor_timeToInterval (this, xclick, clickedTierNumber, & tmin, & tmax);
	IntervalTier intervalTier;
	TextTier textTier;
	_AnyTier_identifyClass (grid -> tiers->at [clickedTierNumber], & intervalTier, & textTier);

	/*
		Get the time of the nearest boundary or point.
	*/
	double tnear = undefined;
	integer clickedLeftBoundary = 0;
	if (intervalTier) {
		const integer clickedIntervalNumber = IntervalTier_timeToIndex (intervalTier, xclick);
		if (clickedIntervalNumber != 0) {
			const TextInterval interval = intervalTier -> intervals.at [clickedIntervalNumber];
			if (xclick > 0.5 * (interval -> xmin + interval -> xmax)) {
				tnear = interval -> xmax;
				clickedLeftBoundary = clickedIntervalNumber + 1;
			} else {
				tnear = interval -> xmin;
				clickedLeftBoundary = clickedIntervalNumber;
			}
		} else {
			/*
				The user clicked outside the time domain of the intervals.
				This can occur when we are grouped with a longer time function.
			*/
			our selectedTier = clickedTierNumber;
			return FunctionEditor_UPDATE_NEEDED;
		}
	} else {
		const integer clickedPointNumber = AnyTier_timeToNearestIndex (textTier->asAnyTier(), xclick);
		if (clickedPointNumber != 0) {
			const TextPoint point = textTier -> points.at [clickedPointNumber];
			tnear = point -> number;
		}
	}
	Melder_assert (! (intervalTier && clickedLeftBoundary == 0));

	/*
		Where did the user click?
	*/
	const bool nearBoundaryOrPoint = ( isdefined (tnear) && fabs (Graphics_dxWCtoMM (our graphics.get(), xclick - tnear)) < 1.5 );
	const integer numberOfTiers = grid -> tiers->size;
	const bool nearCursorCircle = ( our startSelection == our endSelection && Graphics_distanceWCtoMM (our graphics.get(), xclick, yWC,
		our startSelection, (numberOfTiers + 1 - clickedTierNumber) * soundY / numberOfTiers - Graphics_dyMMtoWC (our graphics.get(), 1.5)) < 1.5 );

	/*
		Find out whether this is a click or a drag.
	*/
	bool drag = false;
	while (Graphics_mouseStillDown (our graphics.get())) {
		double x, y;
		Graphics_getMouseLocation (our graphics.get(), & x, & y);
		if (x < our startWindow)
			x = our startWindow;
		if (x > our endWindow)
			x = our endWindow;
		if (fabs (Graphics_dxWCtoMM (our graphics.get(), x - xclick)) > 1.5) {
			drag = true;
			break;
		}
	}

	if (nearBoundaryOrPoint) {
		/*
			Possibility 1: the user clicked near a boundary or point.
			Select or drag it.
		*/
		if (intervalTier && (clickedLeftBoundary < 2 || clickedLeftBoundary > intervalTier -> intervals.size)) {
			/*
				Ignore click on left edge of first interval or right edge of last interval.
			*/
			our selectedTier = clickedTierNumber;
		} else if (drag) {
			/*
				The tier that has been clicked becomes the new selected tier.
				This has to be done before the next Update, i.e. also before do_dragBoundary!
			*/
			our selectedTier = clickedTierNumber;
			do_dragBoundary (this, tnear, clickedTierNumber, shiftKeyPressed);
			return FunctionEditor_NO_UPDATE_NEEDED;
		} else {
			/*
				If the user clicked on an unselected boundary or point, we select it.
			*/
			if (shiftKeyPressed) {
				if (tnear > 0.5 * (our startSelection + our endSelection))
					our endSelection = tnear;
				else
					our startSelection = tnear;
			} else {
				our startSelection = our endSelection = tnear;   // move cursor so that the boundary or point is selected
			}
			our selectedTier = clickedTierNumber;
		}
	} else if (nearCursorCircle) {
		/*
			Possibility 2: the user clicked near the cursor circle.
			Insert boundary or point. There is no danger that we insert on top of an existing boundary or point,
			because we are not 'nearBoundaryOrPoint'.
		*/
		insertBoundaryOrPoint (this, clickedTierNumber, our startSelection, our startSelection, false);
		our selectedTier = clickedTierNumber;
		FunctionEditor_marksChanged (this, true);
		Editor_broadcastDataChanged (this);
		if (drag)
			Graphics_waitMouseUp (our graphics.get());
		return FunctionEditor_NO_UPDATE_NEEDED;
	} else {
		/*
			Possibility 3: the user clicked in empty space.
		*/
		if (intervalTier) {
			our startSelection = tmin;
			our endSelection = tmax;
		}
		selectedTier = clickedTierNumber;
	}
	if (drag)
		Graphics_waitMouseUp (our graphics.get());
	return FunctionEditor_UPDATE_NEEDED;
}

bool structFormantEditor :: v_clickB (double t, double yWC) {
	const double soundY = _FormantEditor_computeSoundY (this);
	if (yWC > soundY) {   // clicked in sound part?
		if (t < our endWindow) {
			our startSelection = t;
			if (our startSelection > our endSelection)
				std::swap (our startSelection, our endSelection);
			return FunctionEditor_UPDATE_NEEDED;
		} else {
			return structTimeSoundEditor :: v_clickB (t, yWC);
		}
	}
	const integer clickedTierNumber = _FormantEditor_yWCtoTier (this, yWC);
	double tmin, tmax;
	_FormantEditor_timeToInterval (this, t, clickedTierNumber, & tmin, & tmax);
	our startSelection = ( t - tmin < tmax - t ? tmin : tmax );   // to nearest boundary
	if (our startSelection > our endSelection)
		std::swap (our startSelection, our endSelection);
	return FunctionEditor_UPDATE_NEEDED;
}

bool structFormantEditor :: v_clickE (double t, double yWC) {
	const double soundY = _FormantEditor_computeSoundY (this);
	if (yWC > soundY) {   // clicked in sound part?
		our endSelection = t;
		if (our startSelection > our endSelection)
			std::swap (our startSelection, our endSelection);
		return FunctionEditor_UPDATE_NEEDED;
	}
	const integer clickedTierNumber = _FormantEditor_yWCtoTier (this, yWC);
	double tmin, tmax;
	_FormantEditor_timeToInterval (this, t, clickedTierNumber, & tmin, & tmax);
	our endSelection = ( t - tmin < tmax - t ? tmin : tmax );
	if (our startSelection > our endSelection)
		std::swap (our startSelection, our endSelection);
	return FunctionEditor_UPDATE_NEEDED;
}

void structFormantEditor :: v_clickSelectionViewer (double xWC, double yWC) {
	const TextGrid grid = our masterSlave.get();
	integer numberOfRows, numberOfColums;
	/*
		On which of the modelers was the click?
	*/
	FormantModelerList fml = formantModelerList.get();
	FormantModelerList_getDisplayLayout (fml, & numberOfRows, & numberOfColums);
	integer numberOfVisible = FormantModelerList_getNumberOfVisible (fml);
	const integer icol = 1 + (int) (xWC * numberOfColums);
	if (icol < 1 || icol > numberOfColums)
		return;
	const integer irow = 1 + (int) ((1.0 - yWC) * numberOfRows);
	if (irow < 1 || irow > numberOfRows)
		return;
	integer index = (irow - 1) * numberOfColums + icol; // left-to-right, top-to-bottom
	for (integer id = 1; id <= fml -> selected.size; id ++)
		fml -> selected [id] = ( index == id ? - abs (fml -> selected [id]) : abs (fml -> selected [id]) );
	
}

void structFormantEditor :: v_play (double tmin, double tmax) {
	if (! d_sound.data && ! d_longSound.data)
		return;
	integer numberOfChannels = ( d_longSound.data ? d_longSound.data -> numberOfChannels : d_sound.data -> ny );
	integer numberOfMuteChannels = 0;
	Melder_assert (our d_sound.muteChannels.size == numberOfChannels);
	for (integer ichan = 1; ichan <= numberOfChannels; ichan ++)
		if (our d_sound.muteChannels [ichan])
			numberOfMuteChannels ++;
	integer numberOfChannelsToPlay = numberOfChannels - numberOfMuteChannels;
	Melder_require (numberOfChannelsToPlay > 0,
		U"Please select at least one channel to play.");
	if (our d_longSound.data) {
		if (numberOfMuteChannels > 0) {
			autoSound part = LongSound_extractPart (our d_longSound.data, tmin, tmax, true);
			autoMixingMatrix thee = MixingMatrix_create (numberOfChannelsToPlay, numberOfChannels);
			MixingMatrix_muteAndActivateChannels (thee.get(), our d_sound.muteChannels.get());
			Sound_MixingMatrix_playPart (part.get(), thee.get(), tmin, tmax, theFunctionEditor_playCallback, this);
		} else {
			LongSound_playPart (our d_longSound.data, tmin, tmax, theFunctionEditor_playCallback, this);
		}
	} else {
		if (numberOfMuteChannels > 0) {
			autoMixingMatrix thee = MixingMatrix_create (numberOfChannelsToPlay, numberOfChannels);
			MixingMatrix_muteAndActivateChannels (thee.get(), our d_sound.muteChannels.get());
			Sound_MixingMatrix_playPart (our d_sound.data, thee.get(), tmin, tmax, theFunctionEditor_playCallback, this);
		} else {
			Sound_playPart (our d_sound.data, tmin, tmax, theFunctionEditor_playCallback, this);
		}
	}
}

void structFormantEditor :: v_updateText () {

}

POSITIVE_VARIABLE (v_prefs_addFields_fontSize)
OPTIONMENU_ENUM_VARIABLE (kGraphics_horizontalAlignment, v_prefs_addFields_textAlignmentInIntervals)
OPTIONMENU_VARIABLE (v_prefs_addFields_useTextStyles)
OPTIONMENU_VARIABLE (v_prefs_addFields_shiftDragMultiple)
OPTIONMENU_ENUM_VARIABLE (kTextGridEditor_showNumberOf, v_prefs_addFields_showNumberOf)
OPTIONMENU_ENUM_VARIABLE (kMelder_string, v_prefs_addFields_paintIntervalsGreenWhoseLabel)
SENTENCE_VARIABLE (v_prefs_addFields_theText)
void structFormantEditor :: v_prefs_addFields (EditorCommand cmd) {
	UiField _radio_;
	POSITIVE_FIELD (v_prefs_addFields_fontSize, U"Font size (points)", our default_fontSize ())
	OPTIONMENU_ENUM_FIELD (kGraphics_horizontalAlignment, v_prefs_addFields_textAlignmentInIntervals,
			U"Text alignment in intervals", kGraphics_horizontalAlignment::DEFAULT)
	OPTIONMENU_FIELD (v_prefs_addFields_useTextStyles, U"The symbols %#_^ in labels", our default_useTextStyles () + 1)
		OPTION (U"are shown as typed")
		OPTION (U"mean italic/bold/sub/super")
	OPTIONMENU_FIELD (v_prefs_addFields_shiftDragMultiple, U"With the shift key, you drag", our default_shiftDragMultiple () + 1)
		OPTION (U"a single boundary")
		OPTION (U"multiple boundaries")
	OPTIONMENU_ENUM_FIELD (kTextGridEditor_showNumberOf, v_prefs_addFields_showNumberOf,
			U"Show number of", kTextGridEditor_showNumberOf::DEFAULT)
	OPTIONMENU_ENUM_FIELD (kMelder_string, v_prefs_addFields_paintIntervalsGreenWhoseLabel,
			U"Paint intervals green whose label...", kMelder_string::DEFAULT)
	SENTENCE_FIELD (v_prefs_addFields_theText, U"...the text", our default_greenString ())
}
void structFormantEditor :: v_prefs_setValues (EditorCommand cmd) {
	SET_OPTION (v_prefs_addFields_useTextStyles, our p_useTextStyles + 1)
	SET_REAL (v_prefs_addFields_fontSize, our p_fontSize)
	SET_ENUM (v_prefs_addFields_textAlignmentInIntervals, kGraphics_horizontalAlignment, our p_alignment)
	SET_OPTION (v_prefs_addFields_shiftDragMultiple, our p_shiftDragMultiple + 1)
	SET_ENUM (v_prefs_addFields_showNumberOf, kTextGridEditor_showNumberOf, our p_showNumberOf)
	SET_ENUM (v_prefs_addFields_paintIntervalsGreenWhoseLabel, kMelder_string, our p_greenMethod)
	SET_STRING (v_prefs_addFields_theText, our p_greenString)
}

void structFormantEditor :: v_prefs_getValues (EditorCommand /* cmd */) {
	our pref_useTextStyles () = our p_useTextStyles = v_prefs_addFields_useTextStyles - 1;
	our pref_fontSize () = our p_fontSize = v_prefs_addFields_fontSize;
	our pref_alignment () = our p_alignment = v_prefs_addFields_textAlignmentInIntervals;
	our pref_shiftDragMultiple () = our p_shiftDragMultiple = v_prefs_addFields_shiftDragMultiple - 1;
	our pref_showNumberOf () = our p_showNumberOf = v_prefs_addFields_showNumberOf;
	our pref_greenMethod () = our p_greenMethod = v_prefs_addFields_paintIntervalsGreenWhoseLabel;
	pref_str32cpy2 (our pref_greenString (), our p_greenString, v_prefs_addFields_theText);
	FunctionEditor_redraw (this);
}

void structFormantEditor :: v_createMenuItems_view_timeDomain (EditorMenu menu) {
	FormantEditor_Parent :: v_createMenuItems_view_timeDomain (menu);
	EditorMenu_addCommand (menu, U"Select previous tier", GuiMenu_OPTION | GuiMenu_UP_ARROW, menu_cb_SelectPreviousTier);
	EditorMenu_addCommand (menu, U"Select next tier", GuiMenu_OPTION | GuiMenu_DOWN_ARROW, menu_cb_SelectNextTier);
	EditorMenu_addCommand (menu, U"Select previous interval", GuiMenu_OPTION | GuiMenu_LEFT_ARROW, menu_cb_SelectPreviousInterval);
	EditorMenu_addCommand (menu, U"Select next interval", GuiMenu_OPTION | GuiMenu_RIGHT_ARROW, menu_cb_SelectNextInterval);
	EditorMenu_addCommand (menu, U"Extend-select left", GuiMenu_SHIFT | GuiMenu_OPTION | GuiMenu_LEFT_ARROW, menu_cb_ExtendSelectPreviousInterval);
	EditorMenu_addCommand (menu, U"Extend-select right", GuiMenu_SHIFT | GuiMenu_OPTION | GuiMenu_RIGHT_ARROW, menu_cb_ExtendSelectNextInterval);
}

void structFormantEditor :: v_highlightSelection (double left, double right, double bottom, double top) {
	if (our v_hasAnalysis () && our p_spectrogram_show && (our d_longSound.data || our d_sound.data)) {
		const double soundY = _FormantEditor_computeSoundY (this), soundY2 = 0.5 * (1.0 + soundY);
		//Graphics_highlight (our graphics.get(), left, right, bottom, soundY * top + (1 - soundY) * bottom);
		Graphics_highlight (our graphics.get(), left, right, soundY2 * top + (1 - soundY2) * bottom, top);
	} else {
		Graphics_highlight (our graphics.get(), left, right, bottom, top);
	}
}

void structFormantEditor :: v_unhighlightSelection (double left, double right, double bottom, double top) {
	if (our v_hasAnalysis () && our p_spectrogram_show && (our d_longSound.data || our d_sound.data)) {
		const double soundY = _FormantEditor_computeSoundY (this), soundY2 = 0.5 * (1.0 + soundY);
		//Graphics_unhighlight (our graphics.get(), left, right, bottom, soundY * top + (1 - soundY) * bottom);
		Graphics_unhighlight (our graphics.get(), left, right, soundY2 * top + (1 - soundY2) * bottom, top);
	} else {
		Graphics_unhighlight (our graphics.get(), left, right, bottom, top);
	}
}

double structFormantEditor :: v_getBottomOfSoundArea () {
	return _FormantEditor_computeSoundY (this);
}

double structFormantEditor :: v_getBottomOfSoundAndAnalysisArea () {
	return _FormantEditor_computeSoundY (this);
}

void structFormantEditor :: v_createMenuItems_pitch_picture (EditorMenu menu) {
	FormantEditor_Parent :: v_createMenuItems_pitch_picture (menu);
	EditorMenu_addCommand (menu, U"Draw visible pitch contour and TextGrid...", 0, menu_cb_DrawTextGridAndPitch);
}

void structFormantEditor :: v_updateMenuItems_file () {
	FormantEditor_Parent :: v_updateMenuItems_file ();
	GuiThing_setSensitive (extractSelectedTextGridPreserveTimesButton, our endSelection > our startSelection);
	GuiThing_setSensitive (extractSelectedTextGridTimeFromZeroButton,  our endSelection > our startSelection);
}

/********** EXPORTED **********/

void FormantEditor_init (FormantEditor me, conststring32 title, Formant formant, Sound sound, bool ownSound, TextGrid grid, conststring32 callbackSocket)
{
	my callbackSocket = Melder_dup (callbackSocket);

	TimeSoundAnalysisEditor_init (me, title, formant, sound, ownSound);

	my selectedTier = 1;
	my v_updateText ();   // to reflect changed tier selection
	if (my endWindow - my startWindow > 30.0) {
		my endWindow = my startWindow + 30.0;
		if (my startWindow == my tmin)
			my startSelection = my endSelection = 0.5 * (my startWindow + my endWindow);
		FunctionEditor_marksChanged (me, false);
	}
	if (sound && sound -> xmin == 0.0 && grid -> xmin != 0.0 && grid -> xmax > sound -> xmax)
		Melder_warning (U"The time domain of the TextGrid (starting at ",
			Melder_fixed (grid -> xmin, 6), U" seconds) does not overlap with that of the sound "
			U"(which starts at 0 seconds).\nIf you want to repair this, you can select the TextGrid "
			U"and choose “Shift times to...” from the Modify menu "
			U"to shift the starting time of the TextGrid to zero.");		
	Melder_require (sound -> xmin == formant -> xmin && sound -> xmax ==  formant -> xmax,
		U"The time domain of the Sound and the Formant should be equal.");
	my masterSlave = Data_copy (grid);
}

void FormantEditor_setMasterSlaveTierPair (FormantEditor me) {
	/*
		Set up the master-slave pair.
	*/
	if (my masterSlave.get() == nullptr) {
		my masterSlave = TextGrid_create (my tmin, my tmax, U"formant-slave formant-master", U"");
		my slaveTierNumber = 1;
		my masterTierNumber = 2;
		return;
	}
	/*
		Does the textgid have a master-slave pair ?
	*/
	const integer numberOfTiers = my masterSlave -> tiers -> size;
	autoSTRVEC tierNames = autoSTRVEC (numberOfTiers);
	for (integer itier = 1; itier <= numberOfTiers; itier ++) {
		const Function anyTier = my masterSlave -> tiers->at [itier];
		tierNames [itier] = Melder_dup (anyTier -> name.get());
	}
	autoSTRVEC toMatch = newSTRVECcopy (tierNames.get());
	STRVECsort_inplace (toMatch.get());
	/*
		<name>-master and <name>-slave are now consecutive
	*/
	integer tierNumber = 0;
	for (integer istr = 1; istr < numberOfTiers; istr ++) {
		const conststring32 isMaster = toMatch [istr] . get();
		const conststring32 isSlave = toMatch [istr + 1] . get();
		if (Melder_stringMatchesCriterion (isMaster, kMelder_string::ENDS_WITH, U"-master", true) &&
			Melder_stringMatchesCriterion (isSlave, kMelder_string::ENDS_WITH, U"-slave", true)) {
				tierNumber = istr;
				break;
		}
	}
	if (tierNumber > 0) {
		integer masterTierNumber, slaveTierNumber = 0;
		const conststring32 master = toMatch [masterTierNumber].get();
		for (integer itier = 1; itier <= numberOfTiers; itier ++) {
			const Function anyTier = my masterSlave -> tiers->at [itier];			
			if (Melder_stringMatchesCriterion (anyTier -> name.get(), kMelder_string::EQUAL_TO, master, true)) {
				masterTierNumber = itier;
				break;
			}
		}
		const conststring32 slave = toMatch [tierNumber + 1].get();
		for (integer itier = 1; itier <= numberOfTiers; itier ++) {
			const Function anyTier = my masterSlave -> tiers->at [itier];			
			if (Melder_stringMatchesCriterion (anyTier -> name.get(), kMelder_string::EQUAL_TO, slave, true)) {
				slaveTierNumber = itier;
				break;
			}
		}
		Melder_assert (slaveTierNumber > 0 && masterTierNumber > 0);
		my masterTierNumber = masterTierNumber;
		my slaveTierNumber = slaveTierNumber;
		return;
	}
	/*
		We didn't find a master-slave pair.
		Add them on top.
	*/
	autoIntervalTier slaveTier = IntervalTier_create (my masterSlave -> xmin, my masterSlave -> xmax);
	my masterSlave -> tiers -> addItemAtPosition_move (slaveTier.move(), 1);
	TextGrid_setTierName (my masterSlave.get(), 1, U"formant-slave");
	my slaveTierNumber = 1;
	autoIntervalTier masterTier = IntervalTier_create (my masterSlave -> xmin, my masterSlave -> xmax);
	my masterSlave -> tiers -> addItemAtPosition_move (masterTier.move(), 2);
	TextGrid_setTierName (my masterSlave.get(), 2, U"formant-master");
	my masterTierNumber = 2;
}

void VowelEditor_setMasterTierLabel (FormantEditor me) {
	FormantAnalysisHistory fah = & my formantListWithHistory -> formantAnalysisHistory;
	kLPC_Analysis lpcType = fah -> lpcType;
	VEC ceilings = my formantListWithHistory -> ceilings.get();
	autoMelderString label;
	for (integer ic = 1; ic <= ceilings.size; ic ++)
		MelderString_append (& label, Melder_iround (ceilings [ic]), U" ");
	MelderString_append (& label, U"; ");
	MelderString_append (& label, kLPC_Analysis_getText (lpcType), U" ");
	MelderString_append (& label, fah -> timeStep, U" ", fah -> maximumNumberOfFormants, U" xxxx ",
		fah -> windowLength, U" ", fah -> preemphasisFrequency);
	if (lpcType == kLPC_Analysis::MARPLE)
		MelderString_append (& label, U" ", fah -> tol1, U" ", fah -> tol2);
	if (lpcType == kLPC_Analysis::ROBUST)
		MelderString_append (& label, U" ", fah -> huberNumberOfStdDev, U" ", fah -> maximumNumberOfIterations,
			U" ", fah -> tol);
	TextGrid_setIntervalText (my masterSlave.get(), 2, 1, label.string);
}

autoFormantEditor FormantEditor_create (conststring32 title, Formant formant, Sound sound, bool ownSound, TextGrid grid, conststring32 callbackSocket) {
	try {
		autoFormantEditor me = Thing_new (FormantEditor);
		FormantEditor_init (me.get(), title, formant, sound, ownSound, grid, callbackSocket);
		if (my p_analysisHistory_lpcType < kLPC_Analysis::AUTOCORRELATION ||
			my p_analysisHistory_lpcType > kLPC_Analysis::BURG)
			my p_analysisHistory_lpcType = my default_analysisHistory_lpcType ();
		if (my p_analysisHistory_timeStep < 0.0)
			my p_analysisHistory_timeStep = Melder_atof (my default_analysisHistory_timeStep ());
		if (my p_analysisHistory_maximumNumberOfFormants <= 0)
			my p_analysisHistory_maximumNumberOfFormants = Melder_atof (my default_analysisHistory_maximumNumberOfFormants ());
		if (my p_analysisHistory_windowLength <= 0.0)
			my p_analysisHistory_windowLength = Melder_atof (my default_analysisHistory_windowLength ());
		if (my p_analysisHistory_preemphasisFrequency <= 0.0)
			my p_analysisHistory_preemphasisFrequency = Melder_atof (my default_analysisHistory_preemphasisFrequency ());
		if (my p_analysisHistory_minimumCeiling <= 0.0)
			my p_analysisHistory_minimumCeiling = Melder_atof (my default_analysisHistory_minimumCeiling ());
		if (my p_analysisHistory_maximumCeiling <= 0.0)
			my p_analysisHistory_maximumCeiling = Melder_atof (my default_analysisHistory_maximumCeiling ());
		if (my p_analysisHistory_numberOfCeilings <= 0)
			my p_analysisHistory_numberOfCeilings = Melder_atoi (my default_analysisHistory_numberOfCeilings ());
		if (my p_analysisHistory_tol1 <= 0.0)
			my p_analysisHistory_tol1 = Melder_atof (my default_analysisHistory_tol1 ());
		if (my p_analysisHistory_tol2 <= 0.0)
			my p_analysisHistory_tol2 = Melder_atof (my default_analysisHistory_tol2 ());
		if (my p_analysisHistory_numberOfStdDev <= 0.0)
			my p_analysisHistory_numberOfStdDev = Melder_atof (my default_analysisHistory_numberOfStdDev ());
		if (my p_analysisHistory_tol <= 0.0)
			my p_analysisHistory_tol = Melder_atof (my default_analysisHistory_tol ());
		if (my p_analysisHistory_maximumNumberOfIterations <= 0)
			my p_analysisHistory_maximumNumberOfIterations = Melder_atoi (my default_analysisHistory_maximumNumberOfIterations ());
		FormantEditor_setMasterSlaveTierPair (me.get());
		my formantListWithHistory = Sound_to_FormantListWithHistory_any (sound, my p_analysisHistory_lpcType,
			my p_analysisHistory_timeStep, my p_analysisHistory_maximumNumberOfFormants,
			my p_analysisHistory_windowLength, my p_analysisHistory_preemphasisFrequency,
			my p_analysisHistory_minimumCeiling, my p_analysisHistory_maximumCeiling,
			my p_analysisHistory_numberOfCeilings, my p_analysisHistory_tol1, my p_analysisHistory_tol2,
			my p_analysisHistory_numberOfStdDev, my p_analysisHistory_tol,
			my p_analysisHistory_maximumNumberOfIterations);
		VowelEditor_setMasterTierLabel (me.get());
		if (my p_modeler_numberOfParametersPerTrack == U"")
			pref_str32cpy2(my p_modeler_numberOfParametersPerTrack, my pref_modeler_numberOfParametersPerTrack (), my default_modeler_numberOfParametersPerTrack ());
		my formantModelerList = FormantListWithHistory_to_FormantModelerList (my formantListWithHistory.get(), my tmin, my tmax, my p_modeler_numberOfParametersPerTrack);
		
		return me;
	} catch (MelderError) {
		Melder_throw (U"FormantEditor window not created.");
	}
}

/* End of file FormantEditor.cpp */
