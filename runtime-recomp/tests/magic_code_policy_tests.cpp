#include "magic_code_policy.hpp"

#include <cassert>
#include <cstdio>

using namespace dkr::runtime::magic_codes;

static_assert(kMagicCodeDefinitions.size() == 24U);
static_assert((kSelectableMagicCodeMask & magic_code_bit(0)) == 0U);
static_assert((kSelectableMagicCodeMask & magic_code_bit(9)) == 0U);
static_assert((kSelectableMagicCodeMask & magic_code_bit(28)) != 0U);
static_assert((kOneShotMagicCodeMask & magic_code_bit(10)) != 0U);
static_assert((kOneShotMagicCodeMask & magic_code_bit(26)) != 0U);
static_assert((kPersistentMagicCodeMask & magic_code_bit(10)) == 0U);

int main() {
    std::uint32_t mask = 0U;
    mask = enable_magic_code(mask, 4);
    mask = enable_magic_code(mask, 5);
    assert(!magic_code_enabled(mask, 4));
    assert(magic_code_enabled(mask, 5));

    mask = enable_magic_code(mask, 12);
    mask = enable_magic_code(mask, 14);
    assert(!magic_code_enabled(mask, 12));
    assert(magic_code_enabled(mask, 14));

    mask = enable_magic_code(mask, 11);
    mask = enable_magic_code(mask, 15);
    assert(!magic_code_enabled(mask, 11));
    assert(magic_code_enabled(mask, 15));
    mask = enable_magic_code(mask, 19);
    assert(!magic_code_enabled(mask, 15));
    assert(magic_code_enabled(mask, 19));
    mask = enable_magic_code(mask, 20);
    assert(magic_code_enabled(mask, 19));
    assert(magic_code_enabled(mask, 20));

    const auto normalised = normalise_magic_code_mask(
        0xFFFFFFFFU);
    assert((normalised & ~kSelectableMagicCodeMask) == 0U);
    assert(!(magic_code_enabled(normalised, 4) &&
             magic_code_enabled(normalised, 5)));
    assert(!(magic_code_enabled(normalised, 11) &&
             (normalised & (magic_code_bit(15) | magic_code_bit(16) |
                            magic_code_bit(17) | magic_code_bit(18) |
                            magic_code_bit(19) | magic_code_bit(20))) != 0U));

    std::puts("[test][magic-code-policy] PASS");
    return 0;
}
