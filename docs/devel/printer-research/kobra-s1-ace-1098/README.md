# Kobra S1 ACE sample files (issue #1098)

Raw reference files supplied by a reporter in
[prestonbrown/helixscreen#1098](https://github.com/prestonbrown/helixscreen/issues/1098)
for the mainline-Python Kobra S1 Klipper fork's ACE Pro integration (tracked in #1069).

| File | What it is |
|------|------------|
| `ace_status.py` | The fork's `moonraker/components/ace_status.py` — defines the `/server/ace/{status,slots,command}` REST surface + `ace:status_update` event, proxied off the `ace` manager + `ace_instance_{N}` Klipper objects. |
| `saved_variables.cfg` | A sample `saved_variables.cfg` (uploaded as `.txt`, renamed back) showing the `ace_inventory_0` shape. Idle/unpopulated. |

Analysis and the corrections these files prompted live in
[`../ANYCUBIC_ACE_KOBRA_S1_LOG_ANALYSIS.md`](../ANYCUBIC_ACE_KOBRA_S1_LOG_ANALYSIS.md)
§ "Sample files received". These are third-party reference artifacts kept verbatim; do
not edit them.
