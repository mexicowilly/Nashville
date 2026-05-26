#pragma once

// ISO 8601 round-trip helpers for std::chrono::system_clock::time_point.
//
// The format produced is `YYYY-MM-DDTHH:MM:SS.sssZ` — exactly the
// shape RFC 3339 (a strict ISO 8601 profile) defines for UTC, with
// millisecond fractional seconds.  Three decimal places, always —
// the formatter pads zeros if the input is coarser than a
// millisecond, and truncates if finer.
//
// Pairing requirement: parse_iso8601 expects exactly the shape that
// format_iso8601 produces.  These are inverses: format then parse
// returns the original time_point (modulo the millisecond
// truncation), and parse then format reproduces the input string
// byte-for-byte.  Strict parsing simplifies the implementation and
// guarantees the round-trip; if you need to consume general ISO
// 8601 strings (varying precision, offset suffixes other than Z,
// no separator between date and time, etc.) write a separate
// function — don't loosen this one.
//
// We use system_clock specifically because it's the only standard
// clock with a defined relationship to civil time.  steady_clock
// and high_resolution_clock are monotonic but have no Unix-epoch
// reference; they can't be meaningfully formatted as a date.
//
// Pre-epoch dates work correctly.  system_clock::time_point is a
// signed duration count, so values before 1970 are representable
// and the chrono calendar code handles them through the proleptic
// Gregorian calendar.  The practical year range is 0000-9999
// because %Y always emits exactly four digits — years outside that
// range would round-trip but would be a project decision worth
// flagging on first use.  Sub-millisecond truncation is toward
// zero (the epoch) in both directions, which gives intuitive
// results across the epoch boundary: -1.5ms past epoch truncates
// to -1ms, which displays as "1969-12-31T23:59:59.999Z" — one
// millisecond before midnight UTC.  -2.5ms past epoch truncates
// to -2ms, displaying as ".998".  The sub-millisecond bits are
// always dropped, never rounded.

#include <chrono>
#include <format>
#include <stdexcept>
#include <string>

namespace nashville
{

// Format a system_clock time_point as `YYYY-MM-DDTHH:MM:SS.sssZ`.
// The input is truncated to millisecond precision before formatting
// — finer-grained input loses its sub-millisecond bits, coarser
// input is padded with zero milliseconds.  Truncation is toward the
// epoch (the standard's `time_point_cast` behavior), matching what
// most users expect of "format with millisecond precision."
inline std::string format_iso8601(std::chrono::system_clock::time_point tp)
{
    // Cast to millisecond precision so the formatter prints exactly
    // three fractional digits.  Without this cast, the output's
    // fractional-second width follows the input's precision: a
    // microseconds time_point would print six digits, a seconds
    // time_point zero.  We want a stable shape across callers, so
    // we normalize first.
    const auto ms = std::chrono::time_point_cast<std::chrono::milliseconds>(tp);
    // `%F` = %Y-%m-%d, `%T` = %H:%M:%S with fractional seconds
    // pulled from the time point's precision.  Append a literal Z
    // since %T doesn't include the offset (we know it's UTC because
    // sys_time formats as UTC by default).
    return std::format("{:%FT%T}Z", ms);
}

// Parse a string produced by format_iso8601 back into a
// system_clock::time_point.  Throws std::runtime_error if the
// string doesn't match the expected shape.  The error message
// includes the offending string so callers can identify which
// input failed up the stack.
//
// We hand-roll the parse rather than using std::chrono::parse
// because std::chrono::parse / from_stream is unimplemented in
// libstdc++ versions older than 14 (this code base targets a
// range of distros where the older versions are common).  The
// hand-rolled parse is fine here because the format is fixed —
// we're only inverting our own formatter, not consuming general
// ISO 8601 strings.
//
// Validation: every digit field is checked for being a digit, and
// every separator is checked for being the exact expected
// character.  std::chrono::sys_days normalizes the date (so e.g.
// "2026-02-30" gets reinterpreted as "2026-03-02") which would
// silently accept nonsense dates; we reject ranges outside the
// civil-time norm before constructing the time point.
inline std::chrono::system_clock::time_point
parse_iso8601(const std::string& s)
{
    using namespace std::chrono;
    // Expected layout: YYYY-MM-DDTHH:MM:SS.sssZ
    //                  0123456789012345678901234
    //                  0         1         2
    // Length 24, fixed.
    auto fail = [&s]() {
        throw std::runtime_error("parse_iso8601: malformed input: " + s);
    };
    if (s.size() != 24) fail();

    auto digit = [&](size_t i) -> int {
        char c = s[i];
        if (c < '0' || c > '9') throw std::runtime_error(
            "parse_iso8601: malformed input: " + s);
        return c - '0';
    };
    auto literal = [&](size_t i, char expected) {
        if (s[i] != expected) throw std::runtime_error(
            "parse_iso8601: malformed input: " + s);
    };

    // Pull each field out by digit position.  No std::stoi /
    // sscanf because both accept signs, leading whitespace, and
    // other slop we don't want here — the round-trip with our
    // formatter is exact, and any deviation is an error.
    const int year   = digit(0)*1000 + digit(1)*100 + digit(2)*10 + digit(3);
    literal(4, '-');
    const int month  = digit(5)*10 + digit(6);
    literal(7, '-');
    const int day    = digit(8)*10 + digit(9);
    literal(10, 'T');
    const int hour   = digit(11)*10 + digit(12);
    literal(13, ':');
    const int minute = digit(14)*10 + digit(15);
    literal(16, ':');
    const int second = digit(17)*10 + digit(18);
    literal(19, '.');
    const int millis = digit(20)*100 + digit(21)*10 + digit(22);
    literal(23, 'Z');

    // Range checks.  std::chrono::year_month_day's is_ok() catches
    // invalid date combinations (e.g. February 30), but the
    // individual hour/minute/second ranges aren't enforced by the
    // sys_time constructor — we'd just get a wrap-around through
    // the duration arithmetic.  Explicit checks here so a corrupt
    // string fails loudly.
    if (month  < 1 || month  > 12) fail();
    if (day    < 1 || day    > 31) fail();
    if (hour   < 0 || hour   > 23) fail();
    if (minute < 0 || minute > 59) fail();
    if (second < 0 || second > 60) fail();  // leap second
    if (millis < 0 || millis > 999) fail();

    const year_month_day ymd{
        std::chrono::year{year},
        std::chrono::month{unsigned(month)},
        std::chrono::day{unsigned(day)} };
    if (!ymd.ok()) fail();   // catches e.g. February 30

    // Assemble: civil date (in UTC, since the input was Z) plus
    // the time-of-day duration since midnight.  sys_days converts
    // the date to a sys_time<days>; adding the duration gives
    // millisecond precision.
    return sys_days{ymd}
         + hours(hour)
         + minutes(minute)
         + seconds(second)
         + milliseconds(millis);
}

} // namespace nashville
