#include "presentation_identity.hpp"

#include <cstdio>

using namespace dkr::runtime::presentation;

constexpr auto kObjectA = make_object_identity(3U, 0x12340U, 7U, 12U, 5U);
constexpr auto kObjectARepeat =
    make_object_identity(3U, 0x12340U, 7U, 12U, 5U);
constexpr auto kObjectNextLife =
    make_object_identity(3U, 0x12340U, 8U, 12U, 5U);
constexpr auto kObjectNextScene =
    make_object_identity(4U, 0x12340U, 7U, 12U, 5U);

static_assert(kObjectA == kObjectARepeat);
static_assert(kObjectA != kObjectNextLife);
static_assert(kObjectA != kObjectNextScene);
static_assert(kObjectA != kIgnoredIdentity && kObjectA != kAutomaticIdentity);
static_assert(make_matrix_identity(kObjectA, 0U) !=
              make_matrix_identity(kObjectA, 1U));
static_assert(make_matrix_identity(kObjectA, 0U) != kIgnoredIdentity);
static_assert(make_matrix_identity(kObjectA, 0U) != kAutomaticIdentity);

int main() {
    std::puts("[test][presentation-identity] PASS");
    return 0;
}

