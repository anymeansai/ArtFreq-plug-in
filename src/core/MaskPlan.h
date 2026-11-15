#pragma once

// The mask plan: the canvas translated into "how much to carve", for ArtFreq's effect.
//
// The sine plan (ArtPlan) turns the drawing into sound; the mask plan turns the same drawing
// into a mask over the incoming audio instead: per canvas column (time) a mask over frequency,
// 0 = leave the audio alone, 1 = carve it as deep as the Depth setting. A spectral carver
// (dsp/SpectralCarver.h) reads it on the audio thread, at whatever frequencies and times its
// own analysis needs, so different carving engines can share one plan.
//
// Like ArtPlan it is built on the message thread from the composited canvas, immutable once
// built, and handed to the audio thread through an exchange (MaskPlanExchange in
// dsp/PlanExchange.h).

#include "Compositor.h"
#include "FrequencyScale.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace artfreq
{

struct MaskSettings
{
    FrequencyScale scale;         // the scale the canvas was composited on: maps rows to Hz
    float threshold = 0.001f;     // canvas values at or below this carve nothing (the sine plan
                                  // treats them as silence too)
    int haloRows = 0;             // widen every drawn shape by this many canvas rows up and
    int haloColumns = 0;          // down, and columns either side (a dark edge around a shape
                                  // that is also played; 0 = exactly the drawing)
};

struct MaskPlan
{
    uint64_t version = 0;  // set by the publisher; strictly increasing per exchange

    FrequencyScale scale;  // rows run evenly over this scale: row 0 at minHz, the last at maxHz
    int numColumns = 0;    // Canvas::width for a built plan
    int numRows = 0;       // Canvas::height for a built plan

    // How much to carve, 0..1, column-major with row 0 at the BOTTOM (the canvas's lowest
    // frequency): values[column * numRows + row]. Row r is centred at scale norm (r + 0.5) / numRows.
    std::vector<float> values;

    // Per column: 1 if any of its values is above 0, so engines can skip work on empty columns.
    std::vector<uint8_t> columnUsed;

    bool empty = true;     // every value is 0: carving changes nothing

    /** The column that a position in the drawing (0..1, a fraction of its length) falls in:
        column c covers [c / numColumns, (c + 1) / numColumns); positions outside 0..1 clamp. */
    int columnAt (double fraction) const noexcept;

    /** The values of one column, numRows of them, bottom row first. */
    const float* column (int c) const noexcept { return values.data() + static_cast<size_t> (c) * static_cast<size_t> (numRows); }

    bool isColumnUsed (int c) const noexcept { return columnUsed[static_cast<size_t> (c)] != 0; }

    /** Where a frequency falls between the rows, in rows: row r's centre is at r. Frequencies
        below the scale's range give values below -0.5, above it values above numRows - 0.5.
        Uses the scale's toNorm (a log on the log scale): engines that sample the same
        frequencies over and over should compute this once per plan, not per sample. */
    double rowAt (double hz) const noexcept;

    /** The mask at a frequency: linear between the two nearest row centres, the edge row's
        value up to the edge of the scale's range, and 0 outside it (the canvas shows nothing
        there, so nothing is carved). */
    float valueAt (int column, double hz) const noexcept;

    /** The same at a height already converted with rowAt. */
    float valueAtRow (int column, double row) const noexcept;

    /** The mean and the maximum of the mask over a span of frequencies (lowHz < highHz), with
        the rows at the ends weighted by how much of them the span covers; the parts outside the
        scale's range count as 0. For engines whose frequency bands are wider than a row (low
        frequencies, short FFTs), where the value at a band's centre can miss a thin stroke.
        Costs one step per row covered. */
    float averageOver (int column, double lowHz, double highHz) const noexcept;
    float maxOver (int column, double lowHz, double highHz) const noexcept;
};

/** Builds a mask plan from a canvas. Each value is the canvas value (0..1, above the threshold;
    else 0), so a half-strength stroke carves half as deep in dB. With a halo, every value is
    first replaced by the maximum within haloRows rows and haloColumns columns of it. */
std::unique_ptr<MaskPlan> buildMaskPlan (const Canvas& canvas, const MaskSettings& settings);

/** A copy of `plan` with every value replaced by the maximum within haloRows rows and
    haloColumns columns of it (the same as building it with that halo). The cost grows with the
    logarithm of the halo, so a wide one in time is cheap. */
std::unique_ptr<MaskPlan> widenMaskPlan (const MaskPlan& plan, int haloRows, int haloColumns);

/** What ArtFreq FX carves with, built together from one canvas: the drawing as it is (Carve,
    Stencil) and the drawing widened by a halo (Carve + Draw, where the sines are played in a hole
    a little larger than they are, so they show with a dark edge). Both are always there, so a
    change of mode, also by automation, takes effect at once rather than after a rebuild on the
    message thread. Immutable once built; travels like a plan (MaskSetExchange in
    dsp/PlanExchange.h). */
struct MaskPlanSet
{
    uint64_t version = 0;  // set by the publisher; strictly increasing per exchange

    std::unique_ptr<MaskPlan> exact;
    std::unique_ptr<MaskPlan> halo;
    int haloRows = 0;      // what `halo` was widened by
    int haloColumns = 0;
};

} // namespace artfreq
