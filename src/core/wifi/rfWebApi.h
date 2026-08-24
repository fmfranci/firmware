#pragma once

// Registers the receive-only CC1101 remote-control routes on Bruce's existing WebUI.
// This module intentionally exposes no RF transmit, replay, brute-force, or jammer actions.
void configureRfWebApi();
