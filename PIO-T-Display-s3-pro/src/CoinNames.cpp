#include "CoinNames.h"
#include <Preferences.h>

#define NAMES_NAMESPACE "names"

// Binance has no public API with coin names, so popular ones are built in
static const struct
{
    const char *ticker;
    const char *name;
} builtinNames[] = {
    {"btc", "Bitcoin"}, {"eth", "Ethereum"}, {"ltc", "Litecoin"}, {"bnb", "BNB"},
    {"sol", "Solana"}, {"xrp", "XRP"}, {"ada", "Cardano"}, {"doge", "Dogecoin"},
    {"trx", "TRON"}, {"ton", "Toncoin"}, {"dot", "Polkadot"}, {"link", "Chainlink"},
    {"avax", "Avalanche"}, {"matic", "Polygon"}, {"pol", "Polygon"}, {"shib", "Shiba Inu"},
    {"bch", "Bitcoin Cash"}, {"xlm", "Stellar"}, {"atom", "Cosmos"}, {"uni", "Uniswap"},
    {"etc", "Ethereum Classic"}, {"near", "NEAR"}, {"apt", "Aptos"}, {"arb", "Arbitrum"},
    {"op", "Optimism"}, {"fil", "Filecoin"}, {"icp", "Internet Computer"}, {"sui", "Sui"},
    {"pepe", "Pepe"}, {"xmr", "Monero"}, {"hbar", "Hedera"}, {"algo", "Algorand"},
    {"aave", "Aave"}, {"inj", "Injective"}, {"kas", "Kaspa"}, {"render", "Render"},
    {"wld", "Worldcoin"}, {"tao", "Bittensor"}, {"fet", "Fetch.ai"}, {"ldo", "Lido DAO"},
    {"mkr", "Maker"}, {"zec", "Zcash"}, {"dash", "Dash"}, {"eos", "EOS"},
    {"xtz", "Tezos"}, {"sand", "The Sandbox"}, {"mana", "Decentraland"}, {"axs", "Axie Infinity"},
    {"crv", "Curve"}, {"snx", "Synthetix"}, {"ftm", "Fantom"}, {"s", "Sonic"},
    {"trump", "TRUMP"}, {"wif", "dogwifhat"}, {"bonk", "Bonk"}, {"floki", "FLOKI"},
    {"ena", "Ethena"}, {"jup", "Jupiter"}, {"sei", "Sei"}, {"tia", "Celestia"},
    {"stx", "Stacks"}, {"imx", "Immutable"}, {"grt", "The Graph"}, {"vet", "VeChain"},
    {"qnt", "Quant"}, {"egld", "MultiversX"}, {"theta", "Theta"}, {"flow", "Flow"},
    {"neo", "NEO"}, {"iota", "IOTA"}, {"zil", "Zilliqa"}, {"ens", "ENS"},
    {"cake", "PancakeSwap"}, {"gala", "Gala"}, {"chz", "Chiliz"}, {"rune", "THORChain"},
    {"paxg", "PAX Gold"}, {"usdc", "USDC"}, {"fdusd", "FDUSD"}, {"not", "Notcoin"},
};

const char *coinBuiltinName(const char *ticker)
{
    for (const auto &entry : builtinNames)
    {
        if (strcmp(entry.ticker, ticker) == 0)
            return entry.name;
    }
    return "";
}

String coinCustomName(const char *ticker)
{
    Preferences prefs;
    String name;
    // Read-write: a read-only open of a missing namespace logs an error
    if (prefs.begin(NAMES_NAMESPACE, false))
    {
        if (prefs.isKey(ticker))
            name = prefs.getString(ticker);
        prefs.end();
    }
    return name;
}

void coinSetCustomName(const char *ticker, String input)
{
    // The screen fonts are ASCII only
    String name;
    for (size_t i = 0; i < input.length(); i++)
    {
        char c = input[i];
        if (c >= 32 && c < 127)
            name += c;
    }
    name.trim();
    if (name.length() > COIN_NAME_MAX)
        name = name.substring(0, COIN_NAME_MAX);

    Preferences prefs;
    prefs.begin(NAMES_NAMESPACE, false);
    if (name.length())
        prefs.putString(ticker, name);
    else if (prefs.isKey(ticker))
        prefs.remove(ticker);
    prefs.end();
}

String coinDisplayName(const char *ticker)
{
    String name = coinCustomName(ticker);
    if (name.length())
        return name;
    name = coinBuiltinName(ticker);
    if (name.length())
        return name;
    name = ticker;
    name.toUpperCase();
    return name;
}
