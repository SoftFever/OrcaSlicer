# /// script
# requires-python = ">=3.12"
#
# [tool.orcaslicer.plugin]
# name = "Network Permission Probe"
# description = "Tests host-based network permission prompts and matching"
# author = "OrcaSlicer"
# version = "0.1.0"
# ///
"""Local probe for the orca.request_permissions(network=...) API.

Install this file as a local plugin, approve https://example.com, then run the
script capability more than once. Each run changes the query string; the saved
scheme/host grant should prevent another prompt for that same host.
"""

import time
import urllib.request

import orca


class NetworkPermissionProbe(orca.script.ScriptPluginCapabilityBase):
    def get_name(self):
        return "Network Permission Probe"

    def execute(self):
        # User-triggered GET to a reserved example domain; varying the query tests
        # that permission matching is not tied to the complete URL.
        url = f"https://example.com/?permission_probe={time.time_ns()}"
        with urllib.request.urlopen(url, timeout=10) as response:
            return orca.ExecutionResult.success(f"Network permission probe: HTTP {response.status}")


@orca.plugin
class NetworkPermissionProbePackage(orca.base):
    def register_capabilities(self):
        orca.request_permissions(network=["https://example.com"])
        orca.register_capability(NetworkPermissionProbe)
