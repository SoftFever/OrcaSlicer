// LICENSE
//
//   This software is dual-licensed to the public domain and under the following
//   license: you are granted a perpetual, irrevocable license to copy, modify,
//   publish, and distribute this file as you see fit.
//
// VERSION 
//   0.2.0  (2017-02-18)  Scored matches perform exhaustive search for best score
//   0.1.0  (2016-03-28)  Initial release
//
// AUTHOR
//   Forrest Smith
//
// NOTES
//   Compiling
//     You MUST add '#define FTS_FUZZY_MATCH_IMPLEMENTATION' before including this header in ONE source file to create implementation.
//
//   fuzzy_match_simple(...)
//     Returns true if each character in pattern is found sequentially within str
//
//   fuzzy_match(...)
//     Returns true if pattern is found AND calculates a score.
//     Performs exhaustive search via recursion to find all possible matches and match with highest score.
//     Scores values have no intrinsic meaning. Possible score range is not normalized and varies with pattern.
//     Recursion is limited internally (default=10) to prevent degenerate cases (pattern="aaaaaa" str="aaaaaaaaaaaaaaaaaaaaaaaaaaaaaa")
//     Uses uint8_t for match indices. Therefore patterns are limited to max_matches characters.
//     Score system should be tuned for YOUR use case. Words, sentences, file names, or method names all prefer different tuning.


#ifndef FTS_FUZZY_MATCH_H
#define FTS_FUZZY_MATCH_H


#include <cstdint> // uint8_t
#include <ctype.h> // ::tolower, ::toupper
#include <cstring> // memcpy

#include <cstdio>

// Public interface
namespace fts {
	using 						char_type 	= wchar_t;
	using 						pos_type  	= uint16_t;
	static constexpr pos_type 	stopper 	= pos_type(-1);
	static constexpr int 		max_matches = 255;

    static bool fuzzy_match(char_type const * pattern, char_type const * str, int & outScore);
    static bool fuzzy_match(char_type const * pattern, char_type const * str, int & outScore, pos_type * matches);
}

#ifdef FTS_FUZZY_MATCH_IMPLEMENTATION
namespace fts {

    // Forward declarations for "private" implementation
    namespace fuzzy_internal {
        static bool fuzzy_match_recursive(const char_type * pattern, const char_type * str, int & outScore, const char_type * strBegin,          
            pos_type const * srcMatches,  pos_type * newMatches, int nextMatch, 
            int & recursionCount, int recursionLimit);
        static void copy_matches(pos_type * dst, pos_type const* src);
    }

    // Public interface
    static bool fuzzy_match(char_type const * pattern, char_type const * str, int & outScore) {
        
        pos_type matches[max_matches + 1]; // with the room for the stopper
        matches[0] = stopper;
        return fuzzy_match(pattern, str, outScore, matches);
    }

    static bool fuzzy_match(char_type const * pattern, char_type const * str, int & outScore, pos_type * matches) {
        int recursionCount = 0;
        int recursionLimit = 10;
        return fuzzy_internal::fuzzy_match_recursive(pattern, str, outScore, str, nullptr, matches, 0, recursionCount, recursionLimit);
    }

    // Private implementation
    static bool fuzzy_internal::fuzzy_match_recursive(
    	// Pattern to match over str.
    	const char_type * 		pattern, 
    	// Text to match the pattern over.
    	const char_type * 		str, 
    	// Score of the pattern matching str. Output variable.
    	int & 					outScore, 
        const char_type * 		strBegin, 
        // Matches when entering this function.
        pos_type const * 		srcMatches,
        // Output matches.
        pos_type * 				matches,
        // Number of matched characters stored in srcMatches when entering this function, also tracking the successive matches.
        int 					nextMatch,
        // Recursion count is input / output to track the maximum depth reached.
        int & 					recursionCount, 
        int 					recursionLimit)
    {
        // Count recursions
        if (++ recursionCount >= recursionLimit)
            return false;

        // Detect end of strings
        if (*pattern == '\0' || *str == '\0')
            return false;

        // Recursion params
        bool recursiveMatch = false;
        pos_type bestRecursiveMatches[max_matches + 1]; // with the room for the stopper
        int bestRecursiveScore = 0;

        // Loop through pattern and str looking for a match
        bool first_match = true;
        while (*pattern != '\0' && *str != '\0') {
            
            // Found match
            if (tolower(*pattern) == tolower(*str)) {

                // Supplied matches buffer was too short
                if (nextMatch >= max_matches)
                    return false;
                
                // "Copy-on-Write" srcMatches into matches
                if (first_match && srcMatches) {
                    memcpy(matches, srcMatches, sizeof(pos_type) * (nextMatch + 1)); // including the stopper
                    first_match = false;
                }

                // Recursive call that "skips" this match
                pos_type recursiveMatches[max_matches];
                int recursiveScore;
                if (fuzzy_match_recursive(pattern, str + 1, recursiveScore, strBegin, matches, recursiveMatches, nextMatch, recursionCount, recursionLimit)) {
                    
                    // Pick best recursive score
                    if (!recursiveMatch || recursiveScore > bestRecursiveScore) {
                    	copy_matches(bestRecursiveMatches, recursiveMatches);
                		bestRecursiveScore = recursiveScore;
                    }
                    recursiveMatch = true;
                }

                // Advance
                matches[nextMatch++] = (pos_type)(str - strBegin);
                // Write a stopper sign.
                matches[nextMatch] = stopper;
                ++pattern;
            }
            ++str;
        }

        // Determine if full pattern was matched
        bool matched = *pattern == '\0' ? true : false;

        // Calculate score
        if (matched) {
            const int sequential_bonus = 15;            // bonus for adjacent matches
            const int separator_bonus = 30;             // bonus if match occurs after a separator
            const int camel_bonus = 30;                 // bonus if match is uppercase and prev is lower
            const int first_letter_bonus = 15;          // bonus if the first letter is matched

            const int leading_letter_penalty = -5;      // penalty applied for every letter in str before the first match
            const int max_leading_letter_penalty = -15; // maximum penalty for leading letters
            const int unmatched_letter_penalty = -1;    // penalty for every letter that doesn't matter

            // Iterate str to end
            while (*str != '\0')
                ++str;

            // Initialize score
            outScore = 100;

            // Apply leading letter penalty
            int penalty = leading_letter_penalty * matches[0];
            if (penalty < max_leading_letter_penalty)
                penalty = max_leading_letter_penalty;
            outScore += penalty;

            // Apply unmatched penalty
            int unmatched = (int)(str - strBegin) - nextMatch;
            outScore += unmatched_letter_penalty * unmatched;

            // Apply ordering bonuses
            for (int i = 0; i < nextMatch; ++i) {
                pos_type currIdx = matches[i];

                if (i > 0) {
                    pos_type prevIdx = matches[i - 1];

                    // Sequential
                    if (currIdx == (prevIdx + 1))
                        outScore += sequential_bonus;
                }

                // Check for bonuses based on neighbor character value
                if (currIdx > 0) {
                    // Camel case
                    // ::islower() expects an unsigned char in range of 0 to 255.
                    char_type uneighbor = strBegin[currIdx - 1];
                    char_type ucurr = strBegin[currIdx];
                    if (std::islower(uneighbor) && std::isupper(ucurr))
                        outScore += camel_bonus;

                    // Separator
                    char_type neighbor = strBegin[currIdx - 1];
                    bool neighborSeparator = neighbor == '_' || neighbor == ' ';
                    if (neighborSeparator)
                        outScore += separator_bonus;
                }
                else {
                    // First letter
                    outScore += first_letter_bonus;
                }
            }
        }

        // Return best result
        if (recursiveMatch && (!matched || bestRecursiveScore > outScore)) {
            // Recursive score is better than "this"
            copy_matches(matches, bestRecursiveMatches);
            outScore = bestRecursiveScore;
            return true;
        }
        else if (matched) {
            // "this" score is better than recursive
            return true;
        }
        else {
            // no match
            return false;
        }
    }

    // Copy matches up to a stopper.
    static void fuzzy_internal::copy_matches(pos_type * dst, pos_type const* src)
    {
        while (*src != stopper)
            *dst++ = *src++;
        *dst = stopper;
    }

} // namespace fts

#endif // FTS_FUZZY_MATCH_IMPLEMENTATION

#endif // FTS_FUZZY_MATCH_H
