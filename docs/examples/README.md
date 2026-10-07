# Provisioning placeholders

`provisioning-settings.example.json` is a credential-free worksheet, not a
Runtime profile or input accepted by the packaging commands. Its `.invalid`
hosts intentionally do not resolve. Replace every placeholder using the owner's
selected publication endpoint, time server, complete product store and private
local directory. No driver identities, byte hashes or live download locations
are invented here.

Use [FIRST_INSTALL.md](../FIRST_INSTALL.md) to generate the pinned full inventory
and private Runtime JSON from actual verified product bytes. Keep `wifi.json`
and every owner/first-install output outside the repository and all publications.
