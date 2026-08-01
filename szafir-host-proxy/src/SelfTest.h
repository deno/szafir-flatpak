#pragma once

namespace SelfTest {

// Verify that the native-messaging FDs survive the full child launch chain:
//   dup2 -> bwrap -> --launch-host (Landlock) -> exec
// Spawns the production bwrap sandbox with /usr/bin/cat standing in for SzafirHost
// and round-trips a message through it. Returns 0 on PASS (or SKIP when child
// wrapping is not enabled), 1 on FAIL.
int fdPassthrough(int argc, char *argv[]);

// Verify HTTPS certificate verification under the production Landlock phases.
// Applies Phase 1 + Phase 2 (honoring LANDLOCK env kill switches), then issues
// a HEAD request through SecureNetwork to the given URL (argv[2], defaults to
// the component download host). Returns 0 when the TLS handshake succeeds.
int tlsProbe(int argc, char *argv[]);

// Verify PC/SC connectivity: establish a context, enumerate readers, and query
// card state. Returns 0 on PASS or SKIP (no pcscd running), 1 on FAIL.
int pcscProbe(int argc, char *argv[]);

// Verify the PKCS#11 provider probe after applying the same Landlock phases
// as the main application. The provider path is argv[2].
int pkcs11Probe(int argc, char *argv[]);

} // namespace SelfTest
