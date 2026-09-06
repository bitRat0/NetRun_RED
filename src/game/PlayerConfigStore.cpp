#include "PlayerConfigStore.h"

#include <Preferences.h>

namespace
{
constexpr const char* kNamespace = "netrun";
constexpr const char* kProfileKey = "runner-v1";
constexpr const char* kDeckKey = "deck-v1";
}

void PlayerConfigStore::load(RunnerProfile& profile, CyberdeckConfig& deck) const
{
    profile = defaultRunnerProfile();
    deck = defaultCyberdeckConfig();
    Preferences preferences;
    if (!preferences.begin(kNamespace, true)) return;

    RunnerProfile storedProfile = profile;
    CyberdeckConfig storedDeck = deck;
    const bool profileRead = preferences.getBytesLength(kProfileKey) == sizeof(storedProfile) &&
        preferences.getBytes(kProfileKey, &storedProfile, sizeof(storedProfile)) == sizeof(storedProfile);
    const bool deckRead = preferences.getBytesLength(kDeckKey) == sizeof(storedDeck) &&
        preferences.getBytes(kDeckKey, &storedDeck, sizeof(storedDeck)) == sizeof(storedDeck);
    preferences.end();

    if (profileRead && runnerProfileValid(storedProfile)) profile = storedProfile;
    if (deckRead && cyberdeckConfigValid(storedDeck)) deck = storedDeck;
}

bool PlayerConfigStore::save(const RunnerProfile& profile, const CyberdeckConfig& deck) const
{
    if (!runnerProfileValid(profile) || !cyberdeckConfigValid(deck)) return false;
    Preferences preferences;
    if (!preferences.begin(kNamespace, false)) return false;
    const bool profileSaved = preferences.putBytes(kProfileKey, &profile, sizeof(profile)) == sizeof(profile);
    const bool deckSaved = preferences.putBytes(kDeckKey, &deck, sizeof(deck)) == sizeof(deck);
    preferences.end();
    return profileSaved && deckSaved;
}

bool PlayerConfigStore::resetDefaults(RunnerProfile& profile, CyberdeckConfig& deck) const
{
    profile = defaultRunnerProfile();
    deck = defaultCyberdeckConfig();
    return save(profile, deck);
}
