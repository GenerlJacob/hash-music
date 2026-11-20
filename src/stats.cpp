// stats.cpp - measures how evenly SHA-256 spreads its output, and charts it (--stats).
#include "stats.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <iomanip>
#include <random>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "mapper.h"
#include "sha256.h"

namespace hashmusic {
namespace {

const char kHexDigits[] = "0123456789abcdef";

std::string with_commas(std::uint64_t n) {
    const std::string digits = std::to_string(n);
    std::string out;
    for (std::size_t i = 0; i < digits.size(); ++i) {
        if (i > 0 && (digits.size() - i) % 3 == 0) out += ',';
        out += digits[i];
    }
    return out;
}

std::string fixed(double value, int places) {
    char buffer[64];
    std::snprintf(buffer, sizeof buffer, "%.*f", places, value);
    return buffer;
}

std::string signed_percent(double fraction) {
    char buffer[64];
    std::snprintf(buffer, sizeof buffer, "%+.1f%%", 100.0 * fraction);
    return buffer;
}

std::string hex_byte(std::size_t value) {
    char buffer[8];
    std::snprintf(buffer, sizeof buffer, "0x%02x", static_cast<unsigned>(value));
    return buffer;
}

// Expected count and standard deviation per bin when `samples` draws land uniformly in `bins` bins.
struct Expectation {
    double count = 0;
    double sigma = 0;
};

Expectation expectation(std::uint64_t samples, std::size_t bins) {
    const double p = 1.0 / static_cast<double>(bins);
    const double n = static_cast<double>(samples);
    return {n * p, std::sqrt(n * p * (1 - p))};
}

// A chi-square statistic is "consistent with uniform" when it is within 3 standard deviations of its
// mean (bins - 1). Its standard deviation is sqrt(2 * (bins - 1)).
bool looks_uniform(double statistic, std::size_t bins) {
    const double df = static_cast<double>(bins - 1);
    return std::abs(statistic - df) <= 3 * std::sqrt(2 * df);
}

std::string chi_square_line(double statistic, std::size_t bins) {
    const double df = static_cast<double>(bins - 1);
    return "chi-square = " + fixed(statistic, 2) + " with " + std::to_string(bins - 1) +
           " degrees of freedom; a uniform source gives " + fixed(df, 0) + " +/- " + fixed(std::sqrt(2 * df), 1);
}

// ---- SVG column chart -------------------------------------------------------------------------

struct ColumnChart {
    double width = 800;                    // viewBox width; the SVG scales to fit its container
    std::vector<std::uint64_t> counts;
    std::vector<std::string> names;        // per column, for tooltips and screen readers
    std::vector<std::string> x_labels;     // label under a column ("" = none)
    std::vector<std::string> x_sublabels;  // optional second line under each column
    Expectation expected;
    std::string expected_text;
    bool focusable = false;                // tab through the columns from the keyboard
    std::string aria_label;
};

// Round the axis up to a clean number with 4-5 gridlines: steps of 1, 2, 2.5 or 5 times a power of 10.
std::pair<double, double> nice_scale(double max_value) {
    const double raw_step = std::max(max_value, 1.0) / 4.0;
    const double magnitude = std::pow(10.0, std::floor(std::log10(raw_step)));
    double step = 10 * magnitude;
    for (const double multiple : {1.0, 2.0, 2.5, 5.0}) {
        if (multiple * magnitude >= raw_step) {
            step = multiple * magnitude;
            break;
        }
    }
    return {std::ceil(max_value / step) * step, step};
}

std::string tick_label(double value, double axis_max) {
    const auto rounded = static_cast<std::uint64_t>(std::llround(value));
    if (axis_max < 10000 || rounded == 0) return with_commas(rounded);
    if (rounded % 1000 == 0) return with_commas(rounded / 1000) + "K";
    return fixed(value / 1000.0, 1) + "K";
}

// A column with a 4px rounded top and a square foot on the baseline.
std::string column_path(double x, double y, double w, double h) {
    const double r = std::min({4.0, w / 2, h});
    std::ostringstream p;
    p << std::fixed << std::setprecision(2);
    p << "M" << x << "," << y + h << " L" << x << "," << y + r << " A" << r << "," << r << " 0 0 1 " << x + r << ","
      << y << " L" << x + w - r << "," << y << " A" << r << "," << r << " 0 0 1 " << x + w << "," << y + r << " L"
      << x + w << "," << y + h << " Z";
    return p.str();
}

std::string column_chart_svg(const ColumnChart& c) {
    const double height = 300;
    const double left = 56;
    const double right = 12;
    const double top = 28;
    const double bottom = c.x_sublabels.empty() ? 30 : 46;
    const double plot_w = c.width - left - right;
    const double plot_h = height - top - bottom;
    const double baseline = top + plot_h;

    double total = 0;
    double highest = c.expected.count + 2 * c.expected.sigma;
    for (const std::uint64_t count : c.counts) {
        total += static_cast<double>(count);
        highest = std::max(highest, static_cast<double>(count));
    }
    const std::pair<double, double> scale = nice_scale(highest);
    const double axis_max = scale.first;
    const double step = scale.second;
    const auto y_of = [&](double value) { return baseline - plot_h * value / axis_max; };
    const double slot = plot_w / static_cast<double>(c.counts.size());
    const double bar_w = std::max(1.0, std::min(24.0, slot - 2.0));

    std::ostringstream s;
    s << std::fixed << std::setprecision(2);
    s << "<svg viewBox=\"0 0 " << c.width << " " << height << "\" role=\"img\" aria-label=\"" << c.aria_label
      << "\">\n";

    // Hairline gridlines and y-axis ticks.
    const auto ticks = static_cast<int>(std::llround(axis_max / step));
    for (int i = 0; i <= ticks; ++i) {
        const double value = step * i;
        const double y = y_of(value);
        if (i > 0) {
            s << "<line class=\"grid\" x1=\"" << left << "\" x2=\"" << left + plot_w << "\" y1=\"" << y << "\" y2=\""
              << y << "\"/>\n";
        }
        s << "<text class=\"tick\" x=\"" << left - 8 << "\" y=\"" << y + 4 << "\" text-anchor=\"end\">"
          << tick_label(value, axis_max) << "</text>\n";
    }

    // Where about 95% of columns land when every value is equally likely: expected +/- 2 sigma.
    const double band_top = y_of(c.expected.count + 2 * c.expected.sigma);
    const double band_bottom = y_of(std::max(0.0, c.expected.count - 2 * c.expected.sigma));
    s << "<rect class=\"band\" x=\"" << left << "\" y=\"" << band_top << "\" width=\"" << plot_w << "\" height=\""
      << band_bottom - band_top << "\"/>\n";

    // Columns. Each hit target is as wide as its slot and as tall as the plot, bigger than the mark.
    for (std::size_t i = 0; i < c.counts.size(); ++i) {
        const double count = static_cast<double>(c.counts[i]);
        const double x = left + slot * static_cast<double>(i);
        const double y = y_of(count);
        const std::string value = with_commas(c.counts[i]) + " (" + fixed(100.0 * count / total, 2) + "%)";
        s << "<g class=\"col\"><path class=\"bar\" d=\"" << column_path(x + (slot - bar_w) / 2, y, bar_w, baseline - y)
          << "\"/><rect class=\"hit\" x=\"" << x << "\" y=\"" << top << "\" width=\"" << slot << "\" height=\""
          << plot_h << "\"" << (c.focusable ? " tabindex=\"0\"" : "") << " data-value=\"" << value
          << "\" data-label=\"" << c.names[i] << "\" aria-label=\"" << c.names[i] << ": " << value << "\"/></g>\n";
    }

    // Baseline over the columns' square feet, then the expected-count reference line and its label.
    s << "<line class=\"baseline\" x1=\"" << left << "\" x2=\"" << left + plot_w << "\" y1=\"" << baseline
      << "\" y2=\"" << baseline << "\"/>\n";
    const double expected_y = y_of(c.expected.count);
    s << "<line class=\"expected\" x1=\"" << left << "\" x2=\"" << left + plot_w << "\" y1=\"" << expected_y
      << "\" y2=\"" << expected_y << "\"/>\n";
    s << "<text class=\"expected-label\" x=\"" << left + plot_w << "\" y=\"" << band_top - 7
      << "\" text-anchor=\"end\">" << c.expected_text << "</text>\n";

    for (std::size_t i = 0; i < c.counts.size(); ++i) {
        const double center = left + slot * (static_cast<double>(i) + 0.5);
        if (!c.x_labels[i].empty()) {
            s << "<text class=\"xlabel\" x=\"" << center << "\" y=\"" << baseline + 18 << "\" text-anchor=\"middle\">"
              << c.x_labels[i] << "</text>\n";
        }
        if (!c.x_sublabels.empty()) {
            s << "<text class=\"xsub\" x=\"" << center << "\" y=\"" << baseline + 34 << "\" text-anchor=\"middle\">"
              << c.x_sublabels[i] << "</text>\n";
        }
    }
    s << "</svg>\n";
    return s.str();
}

// Styles use the reference data-viz palette: series slot 1 (blue), hairline chrome, and dark-mode
// steps chosen for the dark surface rather than inverted.
const char* const kPageStyle = R"CSS(
:root {
  color-scheme: light;
  --page: #f9f9f7; --surface: #fcfcfb; --ink: #0b0b0b; --ink-2: #52514e; --muted: #898781;
  --grid: #e1e0d9; --axis: #c3c2b7; --series: #2a78d6; --border: rgba(11,11,11,0.10);
}
@media (prefers-color-scheme: dark) {
  :root:not([data-theme="light"]) {
    color-scheme: dark;
    --page: #0d0d0d; --surface: #1a1a19; --ink: #f0efec; --ink-2: #c3c2b7; --muted: #898781;
    --grid: #2c2c2a; --axis: #383835; --series: #3987e5; --border: rgba(255,255,255,0.10);
  }
}
:root[data-theme="dark"] {
  color-scheme: dark;
  --page: #0d0d0d; --surface: #1a1a19; --ink: #f0efec; --ink-2: #c3c2b7; --muted: #898781;
  --grid: #2c2c2a; --axis: #383835; --series: #3987e5; --border: rgba(255,255,255,0.10);
}
* { box-sizing: border-box; }
body { margin: 0; background: var(--page); color: var(--ink);
       font: 15px/1.5 system-ui, -apple-system, "Segoe UI", sans-serif; }
main { max-width: 1100px; margin: 0 auto; padding: 24px 16px 48px; }
h1 { font-size: 24px; line-height: 1.25; margin: 0 0 6px; font-weight: 600; }
.lede { color: var(--ink-2); margin: 0 0 20px; max-width: 70ch; }
.tiles { display: grid; grid-template-columns: repeat(auto-fit, minmax(220px, 1fr)); gap: 12px; margin-bottom: 12px; }
.tile, .card { background: var(--surface); border: 1px solid var(--border); border-radius: 12px; padding: 16px; }
.tile .label { color: var(--ink-2); font-size: 13px; }
.tile .value { font-size: 28px; font-weight: 600; line-height: 1.3; }
.tile .note { color: var(--muted); font-size: 13px; }
.verdict { margin: 0 0 20px; color: var(--ink-2); }
.card { margin-bottom: 16px; }
.card h2 { font-size: 16px; margin: 0; font-weight: 600; }
.card .sub { color: var(--ink-2); font-size: 13px; margin: 2px 0 8px; max-width: 80ch; }
svg { display: block; width: 100%; height: auto; overflow: visible; }
.grid { stroke: var(--grid); stroke-width: 1; }
.baseline { stroke: var(--axis); stroke-width: 1; }
.tick { fill: var(--muted); font-size: 12px; font-variant-numeric: tabular-nums; }
.xlabel { fill: var(--ink-2); font-size: 12px; }
.xsub { fill: var(--muted); font-size: 11px; font-family: ui-monospace, Consolas, monospace; }
.bar { fill: var(--series); }
.band { fill: var(--series); fill-opacity: 0.10; }
.expected { stroke: var(--ink-2); stroke-width: 1; }
.expected-label { fill: var(--ink-2); font-size: 12px; paint-order: stroke; stroke: var(--surface); stroke-width: 4px; }
.grid, .baseline, .band, .expected, .expected-label, .tick, .xlabel, .xsub { pointer-events: none; }
.hit { fill: transparent; }
.col:hover .bar, .col:focus-within .bar { filter: brightness(1.18); }
.hit:focus { outline: none; }
.hit:focus-visible { stroke: var(--ink); stroke-width: 1.5; }
details { margin-top: 8px; }
summary { cursor: pointer; color: var(--ink-2); font-size: 13px; }
.table-wrap { overflow-x: auto; }
table { border-collapse: collapse; font-size: 13px; margin-top: 8px; font-variant-numeric: tabular-nums; }
caption { text-align: left; color: var(--muted); font-size: 12px; padding-bottom: 4px; }
th, td { padding: 3px 10px; text-align: right; border-bottom: 1px solid var(--grid); white-space: nowrap; }
th { color: var(--ink-2); font-weight: 600; }
th:first-child, td:first-child { text-align: left; }
.footnote { color: var(--muted); font-size: 13px; max-width: 80ch; }
.tip { position: fixed; z-index: 10; pointer-events: none; background: var(--surface); color: var(--ink);
       border: 1px solid var(--border); border-radius: 8px; padding: 6px 10px; font-size: 13px;
       box-shadow: 0 2px 10px rgba(0,0,0,0.12); }
.tip[hidden] { display: none; }
.tip-value { display: block; font-weight: 600; font-variant-numeric: tabular-nums; }
.tip-label { display: block; color: var(--ink-2); }
)CSS";

// Tooltip: the value leads, the label follows; same details on keyboard focus as on hover.
const char* const kPageScript = R"JS(
(() => {
  const tip = document.getElementById('tip');
  const valueEl = tip.querySelector('.tip-value');
  const labelEl = tip.querySelector('.tip-label');
  function show(hit, x, y) {
    valueEl.textContent = hit.dataset.value;
    labelEl.textContent = hit.dataset.label;
    tip.hidden = false;
    const box = tip.getBoundingClientRect();
    let left = x + 14, top = y - box.height - 14;
    if (left + box.width > window.innerWidth - 8) left = x - box.width - 14;
    if (top < 8) top = y + 14;
    tip.style.left = Math.max(8, left) + 'px';
    tip.style.top = top + 'px';
  }
  function hide() { tip.hidden = true; }
  document.querySelectorAll('.hit').forEach((hit) => {
    hit.addEventListener('pointermove', (e) => show(hit, e.clientX, e.clientY));
    hit.addEventListener('pointerleave', hide);
    hit.addEventListener('focus', () => {
      const r = hit.getBoundingClientRect();
      show(hit, r.left + r.width / 2, r.top + r.height / 3);
    });
    hit.addEventListener('blur', hide);
  });
})();
)JS";

std::string tile(const std::string& label, const std::string& value, const std::string& note) {
    return "<div class=\"tile\"><div class=\"label\">" + label + "</div><div class=\"value\">" + value +
           "</div><div class=\"note\">" + note + "</div></div>\n";
}

}  // namespace

Distribution measure_distribution(std::uint64_t inputs, std::uint64_t seed, HashKind kind) {
    Distribution d;
    d.hash = kind;
    d.inputs = inputs;
    d.seed = seed;
    std::mt19937_64 rng(seed);
    std::string text;
    for (std::uint64_t i = 0; i < inputs; ++i) {
        // At least 8 letters, so inputs (practically) never repeat. With 1-letter strings allowed, only 26
        // exist, so hundreds of inputs repeat, and because hashing is deterministic each repeat adds the
        // same 32 bytes again. That clumping pushed the byte chi-square to about 5 standard deviations
        // above normal in an early version, even though SHA-256 itself was fine.
        text.assign(8 + static_cast<std::size_t>(rng() % 17), ' ');
        for (char& c : text) c = static_cast<char>('a' + rng() % 26);
        const Digest digest = hash_text(kind, text);
        for (std::size_t b = 0; b < digest.size(); ++b) {
            ++d.byte_counts[digest[b]];
            if (b < kMelodyNotes) {
                ++d.pitch_counts[digest[b] >> 4];
                ++d.rhythm_counts[digest[b] & 0x0F];
            }
        }
    }
    return d;
}

std::string distribution_text(const Distribution& d) {
    const std::uint64_t notes = d.inputs * kMelodyNotes;
    const std::uint64_t bytes = d.inputs * kDigestBytes;
    const double pitch_chi = chi_square(d.pitch_counts);
    const double byte_chi = chi_square(d.byte_counts);
    std::ostringstream out;
    out << "Distribution check (" << hash_display_name(d.hash) << "): " << with_commas(d.inputs)
        << " random inputs (lowercase strings of 8-24 letters, seed " << d.seed << ")\n\n";

    out << "Melody pitches (first hex digit of bytes 0-23): " << with_commas(notes) << " notes, "
        << with_commas(static_cast<std::uint64_t>(std::llround(expectation(notes, 16).count)))
        << " expected per pitch\n";
    const std::uint64_t most = std::max<std::uint64_t>(1, *std::max_element(d.pitch_counts.begin(), d.pitch_counts.end()));
    for (std::size_t i = 0; i < 16; ++i) {
        const std::uint64_t count = d.pitch_counts[i];
        const auto bar = static_cast<std::size_t>(std::llround(40.0 * static_cast<double>(count) / static_cast<double>(most)));
        std::string name = note_name(kPitchTable[i]);
        name.resize(3, ' ');
        out << "  " << kHexDigits[i] << " " << name << " " << std::string(bar, '#') << std::string(41 - bar, ' ')
            << std::setw(9) << with_commas(count) << "  "
            << fixed(notes > 0 ? 100.0 * static_cast<double>(count) / static_cast<double>(notes) : 0.0, 2) << "%\n";
    }
    out << "  " << chi_square_line(pitch_chi, 16) << "\n\n";

    const auto fewest = std::min_element(d.byte_counts.begin(), d.byte_counts.end());
    const auto commonest = std::max_element(d.byte_counts.begin(), d.byte_counts.end());
    out << "Digest bytes (all 32 bytes): " << with_commas(bytes) << " bytes, "
        << with_commas(static_cast<std::uint64_t>(std::llround(expectation(bytes, 256).count)))
        << " expected per value\n";
    out << "  rarest value " << hex_byte(static_cast<std::size_t>(fewest - d.byte_counts.begin())) << " appeared "
        << with_commas(*fewest) << " times; commonest " << hex_byte(static_cast<std::size_t>(commonest - d.byte_counts.begin()))
        << " appeared " << with_commas(*commonest) << " times\n";
    out << "  " << chi_square_line(byte_chi, 256) << "\n\n";

    if (looks_uniform(pitch_chi, 16) && looks_uniform(byte_chi, 256)) {
        out << "Both statistics are within 3 standard deviations of a perfectly uniform source: no pitch or byte value is favored.\n";
    } else {
        out << "At least one statistic is more than 3 standard deviations from a uniform source: some values come up more often than chance allows.\n";
    }
    return out.str();
}

std::string distribution_html(const Distribution& d) {
    const std::uint64_t notes = d.inputs * kMelodyNotes;
    const std::uint64_t bytes = d.inputs * kDigestBytes;
    const Expectation pitch_expected = expectation(notes, 16);
    const Expectation byte_expected = expectation(bytes, 256);
    const double pitch_chi = chi_square(d.pitch_counts);
    const double byte_chi = chi_square(d.byte_counts);
    const auto round_count = [](double value) { return with_commas(static_cast<std::uint64_t>(std::llround(value))); };

    ColumnChart pitch;
    pitch.width = 800;
    for (std::size_t i = 0; i < 16; ++i) {
        const std::string name = note_name(kPitchTable[i]);
        pitch.counts.push_back(d.pitch_counts[i]);
        pitch.names.push_back(name + " (hex digit " + kHexDigits[i] + ")");
        pitch.x_labels.push_back(name);
        pitch.x_sublabels.push_back(std::string(1, kHexDigits[i]));
    }
    pitch.expected = pitch_expected;
    pitch.expected_text = "expected if uniform: " + round_count(pitch_expected.count);
    pitch.focusable = true;
    pitch.aria_label = "Column chart of how many melody notes landed on each of the 16 pitches";

    ColumnChart byte;
    byte.width = 1060;
    for (std::size_t value = 0; value < 256; ++value) {
        byte.counts.push_back(d.byte_counts[value]);
        byte.names.push_back("byte value " + hex_byte(value));
        byte.x_labels.push_back(value % 32 == 0 || value == 255 ? hex_byte(value) : "");
    }
    byte.expected = byte_expected;
    byte.expected_text = "expected if uniform: " + round_count(byte_expected.count);
    byte.aria_label = "Column chart of how often each byte value from 0x00 to 0xff appeared in the digests";

    std::ostringstream h;
    h << "<!doctype html>\n<html lang=\"en\">\n<head>\n<meta charset=\"utf-8\">\n"
      << "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n"
      << "<title>SHA-256 distribution</title>\n<style>" << kPageStyle << "</style>\n</head>\n<body>\n<main>\n";

    h << "<h1>Is " << (d.hash == HashKind::kToy ? "the toy XOR hash" : "SHA-256") << "&rsquo;s output evenly spread?</h1>\n"
      << "<p class=\"lede\">We hashed " << with_commas(d.inputs)
      << " random inputs (lowercase strings of 8&ndash;24 letters, seed " << d.seed
      << ") with our own " << hash_display_name(d.hash) << ". If the output is uniform, every pitch and every byte value turns up about equally "
         "often, so the columns in each chart should be level, wobbling only inside the shaded band.</p>\n";

    h << "<div class=\"tiles\">\n"
      << tile("Inputs hashed", with_commas(d.inputs),
              with_commas(bytes) + " digest bytes &middot; " + with_commas(notes) + " melody notes")
      << tile("Pitch chi-square", fixed(pitch_chi, 1),
              "uniform gives 15 &plusmn; " + fixed(std::sqrt(30.0), 1) + " (15 degrees of freedom)")
      << tile("Byte chi-square", fixed(byte_chi, 1),
              "uniform gives 255 &plusmn; " + fixed(std::sqrt(510.0), 1) + " (255 degrees of freedom)")
      << "</div>\n";
    h << "<p class=\"verdict\">"
      << (looks_uniform(pitch_chi, 16) && looks_uniform(byte_chi, 256)
              ? "Both statistics are within 3 standard deviations of what a perfectly uniform source gives: no pitch "
                "or byte value is favored."
              : "At least one statistic is more than 3 standard deviations from what a uniform source gives: some "
                "values come up more often than chance allows.")
      << "</p>\n";

    // Card 1: pitches.
    h << "<section class=\"card\">\n<h2>Melody pitches</h2>\n<p class=\"sub\">How many of the " << with_commas(notes)
      << " melody notes landed on each pitch. The hex digit under each name is the first digit of the byte that "
         "picks it. The shaded band (expected &plusmn; 2&sigma;, where about 95% of columns land by chance) is only "
         "a hair wide here: with this many notes per pitch, chance barely moves the counts.</p>\n"
      << column_chart_svg(pitch)
      << "<details><summary>Table view</summary><div class=\"table-wrap\"><table>\n"
      << "<tr><th>Hex digit</th><th>Pitch</th><th>Notes</th><th>Share</th><th>vs expected</th></tr>\n";
    for (std::size_t i = 0; i < 16; ++i) {
        const double count = static_cast<double>(d.pitch_counts[i]);
        h << "<tr><td>" << kHexDigits[i] << "</td><td>" << note_name(kPitchTable[i]) << "</td><td>"
          << with_commas(d.pitch_counts[i]) << "</td><td>" << fixed(notes ? 100.0 * count / static_cast<double>(notes) : 0, 2)
          << "%</td><td>" << signed_percent(pitch_expected.count > 0 ? count / pitch_expected.count - 1 : 0)
          << "</td></tr>\n";
    }
    h << "</table></div></details>\n</section>\n";

    // Card 2: byte values.
    h << "<section class=\"card\">\n<h2>Digest byte values</h2>\n<p class=\"sub\">How often each value from 0x00 to "
         "0xff appeared among all "
      << with_commas(bytes)
      << " digest bytes. Shaded band: expected &plusmn; 2&sigma;, where about 95% of columns land by chance.</p>\n"
      << column_chart_svg(byte)
      << "<details><summary>Table view</summary><div class=\"table-wrap\"><table>\n"
      << "<caption>Count of each byte value. Row = first hex digit, column = second hex digit.</caption>\n<tr><th></th>";
    for (std::size_t col = 0; col < 16; ++col) h << "<th>" << kHexDigits[col] << "</th>";
    h << "</tr>\n";
    for (std::size_t row = 0; row < 16; ++row) {
        h << "<tr><th>" << kHexDigits[row] << "</th>";
        for (std::size_t col = 0; col < 16; ++col) h << "<td>" << with_commas(d.byte_counts[row * 16 + col]) << "</td>";
        h << "</tr>\n";
    }
    h << "</table></div></details>\n</section>\n";

    h << "<p class=\"footnote\">Chi-square adds up (observed &minus; expected)&sup2; &divide; expected over every "
         "column. A uniform source scores about the number of columns minus one; a hash that favored some values would "
         "score far higher. Generated by <code>hashmusic --stats</code>.</p>\n"
      << "</main>\n<div class=\"tip\" id=\"tip\" hidden><span class=\"tip-value\"></span><span class=\"tip-label\"></span></div>\n"
      << "<script>" << kPageScript << "</script>\n</body>\n</html>\n";
    return h.str();
}

std::string distribution_csv(const Distribution& d) {
    std::ostringstream out;
    out << "kind,value,label,count\n";
    for (std::size_t i = 0; i < 16; ++i) {
        out << "pitch," << kHexDigits[i] << "," << note_name(kPitchTable[i]) << "," << d.pitch_counts[i] << "\n";
    }
    for (std::size_t i = 0; i < 16; ++i) {
        out << "rhythm," << kHexDigits[i] << "," << rhythm_symbol(kRhythmTable[i]) << "," << d.rhythm_counts[i] << "\n";
    }
    for (std::size_t value = 0; value < 256; ++value) {
        out << "byte," << hex_byte(value) << ",," << d.byte_counts[value] << "\n";
    }
    return out.str();
}

}  // namespace hashmusic
