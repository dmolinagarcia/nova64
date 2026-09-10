# Self-Hosted Codespaces on OCI Always Free — Complete Build Runbook
> the development environment · built from a browser · and extracted from this sheet

**Supersedes:** `dev-environment-runbook.md`, `oci-remote-desktop-guide.md`, `devbox-oci-setup.sh` and
`devbox-desktop-setup.sh`. This document is self-contained: every script, template and configuration file
is reproduced here in full and can be extracted automatically (section 4). What changed relative to those
sources, and why, is listed in Appendix D.

- **Host:** OCI Always Free `VM.Standard.A1.Flex`, 2 OCPU / 12 GB, Ubuntu 24.04 aarch64, tenancy home region
- **Access model:** browser-only, through two independent doors
- **Door A** — `code tunnel` → `https://vscode.dev/tunnel/<name>` (outbound 443 only)
- **Door B** — `code-server` behind Caddy → `https://<name>.<BASE_DOMAIN>` (inbound 443)
- **Environment model:** one container per project, defined by `devcontainer.json`
- **Management:** a single `dev` command wrapping the whole lifecycle
- **Administration:** OCI Cloud Shell, so even the setup needs nothing but a browser
- **Optional:** an XFCE desktop in the browser (Guacamole), published on the same edge — section 12

---

## Table of contents

1. What this builds
2. Why the editor runs inside the container
3. Constraints worth knowing before you start
4. How to use this document
5. Phase 0 — Prerequisites
6. Phase 1 — Provision the OCI infrastructure
7. Phase 2 — Stable public IP and DNS
8. Phase 3 — Host setup
9. Phase 4 — Configure and start the edge
10. Phase 5 — First project and tunnel login
11. Phase 6 — Validation
12. Phase 7 (optional) — Web desktop with Guacamole
13. Daily operation
14. Vendor and proprietary toolchains
15. Backup and maintenance
16. Troubleshooting
17. Known limitations
18. Reference — environment files
- Appendix A — devbox-oci-setup.sh
- Appendix B — devbox-host-setup.sh
- Appendix C — devbox-desktop-setup.sh
- Appendix D — Changes from the source documents

---

## 1. What this builds

Access paths:

```
door A   browser ── HTTPS 443 ──► vscode.dev relay ◄── outbound 443 ── code tunnel ─────┐
door B   browser ── HTTPS 443 ──► devenv-caddy ──► code-server :8080 ──────────────────┴─ project container
desktop  browser ── HTTPS 443 ──► devenv-caddy ──► guacamole ──► guacd ──► xrdp ──► XFCE   (optional)
admin    OCI Cloud Shell ── SSH 22, key only ──► instance
```

Host layout:

```
OCI instance "devbox" — VM.Standard.A1.Flex, 2 OCPU / 12 GB, Ubuntu 24.04 aarch64
│
├── Docker Engine
│   │
│   ├── devenv-caddy               TLS termination for door B and the desktop
│   │     :80 :443 published       one Let's Encrypt certificate per hostname
│   │
│   ├── devenv-nova64              project container, built from devcontainer.json
│   │     ├── code tunnel ─────────► outbound 443 ──► vscode.dev/tunnel/nova64   (A)
│   │     ├── code-server :8080 ───► devenv_edge ───► caddy ──► nova64.<domain>  (B)
│   │     └── /workspaces/nova64  ← bind mount from the host
│   │
│   ├── devenv-<project>           same pattern, unique tunnel name
│   │
│   └── desktop stack (optional)   guacamole, guacd, postgres — section 12
│
├── xrdp :3389                     optional; accepted only from Docker networks
└── /srv/dev/                      host-side state, scripts and configuration
```

Both doors serve the same container, the same files and the same processes. They differ only in transport
and in which extension marketplace is reachable: door A reaches the official Marketplace (Copilot and
Microsoft-proprietary extensions work), door B reaches Open VSX only.

Running both is deliberate. The failure this environment exists to solve is a restrictive network, and the
two doors fail independently: proxies with TLS inspection sometimes block `vscode.dev` or break its
WebSocket upgrade, while a plain HTTPS request to your own domain still passes.

Inbound, the instance accepts 22 (SSH, key only), 80 (ACME challenge and redirects) and 443. Everything else
is rejected by the image's host firewall, and the OCI security list opens nothing more. code-server's port
8080 is never published, and the desktop's xrdp and Guacamole ports are not reachable from outside.

The build is three scripts plus one management command, all contained in this document:

| Piece | Runs on | Does |
|---|---|---|
| `devbox-oci-setup.sh` (Appendix A) | Cloud Shell | compartment, network, security list, SSH key, instance |
| `devbox-host-setup.sh` (Appendix B) | the instance | Docker, Dev Container CLI, swap, `/srv/dev`, Git key, fail2ban |
| `dev` (section 18.8) | the instance | project lifecycle: add, build, update, login, status, backup |
| `devbox-desktop-setup.sh` (Appendix C) | the instance, optional | XFCE, xrdp and Guacamole behind the same edge |

All of them are idempotent and safe to re-run.

---

## 2. Why the editor runs inside the container

The Dev Containers extension cannot be installed when connecting to a remote host from `vscode.dev` in a
browser. It works from desktop VS Code against the same host, but not from the web client. The
"browser → host → container" path that GitHub Codespaces appears to offer is therefore not reproducible from
a browser.

The design consequence is structural: **the editor server runs inside the project container**, and the
browser connects directly to it. This is what Codespaces does internally. `devcontainer.json` is still the
source of truth for the image, but it is used at *build* time only — never at connect time.

---

## 3. Constraints worth knowing before you start

**The Ampere A1 free allowance was halved.** Oracle reduced it from 4 OCPUs / 24 GB to 2 OCPUs / 12 GB
(1,500 OCPU-hours and 9,000 GB-hours per month) effective 15 June 2026, without a public announcement.
Free-tier accounts were emailed that instances above the new limit would be disabled from 18 August 2026.
Reports on whether Pay As You Go accounts kept the old allocation contradict each other. Size the instance
at 2 / 12; the provisioning script checks the real quota before launching.

**Always Free resources only exist in your home region.** That region was fixed at signup and cannot be
changed. The provisioning script defaults to `eu-frankfurt-1` (roughly 30–40 ms from Madrid) and refuses to
run against any region other than the tenancy's home region. Frankfurt has three availability domains, which
helps with capacity; most regions have only one.

**A1 capacity is frequently exhausted.** `Out of host capacity` on instance launch is normal, not a
misconfiguration. The provisioning script rotates through all availability domains and retries. Cloud Shell
sessions time out after 20 minutes without keyboard activity and last 24 hours at most: for a long capacity
fight, keep the session active or run the script from a machine that stays on.

**Idle instances can be reclaimed on Free Tier accounts.** Oracle's Always Free documentation lets it
reclaim Always Free compute instances that stay idle — low CPU, network and memory utilisation — over a
7-day window. A development box idles most of the week. Oracle's own notice states that converting the
account to Pay As You Go prevents this, and Always Free resources stay free after the upgrade. Recommended,
together with the budget alert in section 5.

**The host is ARM64.** Everything in this runbook is architecture-aware, and the open-source toolchains in
Debian's archive (GCC, Yosys / nextpnr / IceStorm, cc65, KiCad and many more) are built for arm64. Vendor
tools shipped only as x86-64 binaries do not run natively; legacy Xilinx ISE (Spartan-6) is the obvious
example. See section 14.

**Do not use ufw.** Oracle's Ubuntu images ship iptables rules in `/etc/iptables/rules.v4` that allow SSH,
reject everything else, and keep the instance's volumes reachable. Oracle warns that enabling ufw can leave
the instance unable to boot. This runbook never touches ufw: Docker publishes Caddy's ports through the
`FORWARD` chain it manages, and the single host rule the optional desktop needs is added to `rules.v4`
directly.

**Door B needs a domain you own.** Each project gets its own hostname under a wildcard DNS record. Free
dynamic DNS providers are a poor fit: FreeDNS (afraid.org) shared domains are not on the Public Suffix List —
inclusion requests must come from the domain registrant, and the proposals never progressed — and Let's
Encrypt counts issuance limits per registered domain, so every user of a shared domain draws from the same
quota and `too many certificates already issued` is common. Without a domain, door A still works on its own.

**Bare-IP HTTPS is not used.** The TLS specification does not permit address literals in the Server Name
Indication extension, so a browser opening `https://<ip>` sends no SNI at all; Caddy selects certificates by
SNI, and the handshake fails with `ERR_SSL_PROTOCOL_ERROR`. The source desktop guide worked around this with
`default_sni` plus either Caddy's internal CA or a short-lived Let's Encrypt IP certificate (generally
available since 15 January 2026, about 160 hours of validity). With a domain none of that is needed: every
hostname, the desktop included, gets an ordinary certificate automatically.

---

## 4. How to use this document

Every file in this runbook sits in a fenced code block preceded by a hidden marker naming its path (an HTML
comment, visible in the raw Markdown). The extractor below writes each block to `~/devbox-kit/<path>` byte
for byte and marks scripts executable. It avoids copying long scripts through a browser terminal, which is
exactly where tabs, long lines and heredocs get mangled.

1. Open Cloud Shell (OCI Console → Developer tools → Cloud Shell) and fetch this sheet, which is the file the extractor reads:

~~~bash
curl -fsSL -o devbox-oci-runbook.md \
  https://raw.githubusercontent.com/dmolinagarcia/nova64/main/docs/docsV3/content/sec_ai_em0.md
~~~

Cloud Shell's Upload option does the same if you already have the file, and so does any Linux or macOS machine.
2. Run, in the directory holding the file:

~~~bash
awk -v kit="$HOME/devbox-kit" '
{ sub(/\r$/, "") }
/^<!-- file: [^ ]+ -->$/ { path = $3; next }
path != "" && !body && /^```/ {
    body = 1; n = 0; shebang = 0; out = kit "/" path
    dir = out; sub(/\/[^\/]*$/, "", dir); system("mkdir -p \"" dir "\"")
    printf "" > out
    next
}
body && /^```$/ {
    body = 0; close(out)
    if (shebang) system("chmod +x \"" out "\"")
    print "  " out
    path = ""
    next
}
body { if (++n == 1 && /^#!/) shebang = 1; print > out }
' devbox-oci-runbook.md
~~~

3. You should get eleven files:

```
devbox-kit/
├── devbox-oci-setup.sh            Appendix A — run in Cloud Shell
├── devbox-host-setup.sh           Appendix B — run on the instance
├── devbox-desktop-setup.sh        Appendix C — optional, run on the instance
└── srv/dev/                       section 18 — installed into /srv/dev by Appendix B
    ├── config.env
    ├── compose.base.yml
    ├── caddy/Caddyfile
    ├── images/entrypoint.sh
    ├── templates/devenv.compose.yml.tpl
    ├── templates/site.caddy.tpl
    ├── templates/Dockerfile.tpl
    └── bin/dev
```

If you edit a file inside this document, keep its marker line directly above its code block. Re-running the
extractor overwrites the kit, never anything under `/srv/dev`.

---

## 5. Phase 0 — Prerequisites

1. **An OCI account.** Note its home region; the Console shows it in the region selector and on the tenancy details page.
2. **A decision on Pay As You Go.** Recommended because of the idle-reclamation policy (section 3): Billing & Cost Management → Upgrade and Manage Payment. Oracle does not charge for Always Free resources after the upgrade, only for usage above the Always Free limits.
3. **A budget alert**, whatever the account type: Billing & Cost Management → Budgets → Create Budget, target the root compartment, amount 1 EUR, alert rule at 1 % of actual spend, your email. Oracle has changed the A1 allowance without notice more than once; this gets you an email within hours rather than a surprise at month end.
4. **A workstation.** OCI Cloud Shell is recommended: it runs in the browser, has the OCI CLI already authenticated, keeps a persistent home directory and can open outbound SSH. Any Linux or macOS machine with the OCI CLI (`oci setup config`) and `ssh-keygen` also works.
5. **A domain you control**, with DNS you can edit. Section 7 creates one wildcard record. If the domain publishes CAA records, they must allow `letsencrypt.org`.
6. **A GitHub account.** Door A signs in with it, and the devbox's Git key is added to it in section 8.

The scripts fix these names: compartment `devbox`, VCN `vcn-devbox`, instance `devbox`. Project names become
tunnel names and hostnames: lowercase letters, digits and hyphens, at most 20 characters.

Optional quota check before starting (the script repeats it):

```bash
oci limits resource-availability get \
  --service-name compute \
  --limit-name standard-a1-core-count \
  --compartment-id <tenancy-ocid> \
  --availability-domain <ad-name>
```

---

## 6. Phase 1 — Provision the OCI infrastructure

From Cloud Shell, after section 4:

```bash
bash ~/devbox-kit/devbox-oci-setup.sh
```

It creates, in order: the `devbox` compartment, VCN `vcn-devbox` (10.0.0.0/16), an internet gateway, a
default route, a public subnet (10.0.0.0/24), ingress rules for TCP 80 and 443 on the default security list,
an SSH keypair (`~/.ssh/oci_devbox`) and the `devbox` instance.

Notable behaviours:

- Every resource is looked up by name before creation, so re-running after a failed launch reuses what exists instead of duplicating it. An existing instance in any state other than terminated is reused — a stopped one is started — so a re-run never launches a second instance.
- It refuses to run against a region other than the tenancy's home region. If yours is not Frankfurt: `REGION=<home-region> bash ~/devbox-kit/devbox-oci-setup.sh`.
- On `OutOfHostCapacity` it rotates through all availability domains and retries every 60 seconds. On any other error it stops and prints the message, rather than hammering the API indefinitely.
- Ingress for 80 and 443 is added only if missing, and it is safe to add before anything listens: the image's host firewall rejects everything except SSH until Docker publishes Caddy. Port 80 stays open to `0.0.0.0/0` because ACME validation comes from many addresses; 443 as well, because door B must be reachable from every network you work from. `OPEN_WEB=false` leaves the security list alone.
- The boot volume is 100 GB with default VPU. Size is free within the 200 GB block storage allowance; raising the performance tier is not.
- Resulting OCIDs and the public IP are written to `~/devbox.env`.

When it finishes:

```bash
source ~/devbox.env
ssh -i ~/.ssh/oci_devbox ubuntu@$IP      # or simply: devbox
```

**Back up the SSH private key.** Download `~/.ssh/oci_devbox` (Cloud Shell's menu has a Download option) and
keep it somewhere safe. It is the only credential that opens SSH on the instance, and Oracle removes Cloud
Shell home directories after a long period without use.

To manage the ingress rules by hand instead: Networking → Virtual Cloud Networks → `vcn-devbox` → Security
Lists → Default → Add Ingress Rules.

| Protocol | Port | Source | Purpose |
|---|---|---|---|
| TCP | 80 | 0.0.0.0/0 | ACME HTTP challenge, HTTP → HTTPS redirects |
| TCP | 443 | 0.0.0.0/0 | door B and the optional desktop |

Your workstation's egress address and the instance's public address are different things. Get the former
with `curl -s ifconfig.me` from the workstation, and the latter from the metadata service on the instance:

```bash
curl -s -H 'Authorization: Bearer Oracle' http://169.254.169.254/opc/v2/vnics/ | grep publicIp
```

---

## 7. Phase 2 — Stable public IP and DNS

The instance received an ephemeral public IP, which lives and dies with the instance. A reserved IP survives
termination and can be attached to a replacement, so DNS never has to change. Switch before creating any DNS
record: the address changes when you do.

1. **Reserve the IP.** Console → Compute → Instances → `devbox` → Attached VNICs → the primary VNIC → IPv4 Addresses → edit the primary private IP → Public IP type: **Reserved public IP** → create a new one (for example `devbox-ip`) → save. Oracle announced reserved public IPs as free of charge; the budget alert from section 5 is the backstop if that ever changes.
2. **Refresh `~/devbox.env`.** Re-run the provisioning script; it reuses everything and rewrites the file with the new address:

   ```bash
   bash ~/devbox-kit/devbox-oci-setup.sh && source ~/devbox.env && echo "$IP"
   ```

3. **Create the DNS record.** Choose the base domain for door B — this runbook uses `dev.example.com` — and create a wildcard A record pointing at the reserved IP:

   ```
   *.dev.example.com.   300   IN   A   <reserved IP>
   ```

Projects become `<project>.dev.example.com` and the optional desktop `desktop.dev.example.com`. A wildcard means `dev add` never needs a DNS step.
4. **Verify before continuing.** Caddy requests certificates as soon as a site appears, and repeated failures count against Let's Encrypt's limits:

   ```bash
   dig +short anything.dev.example.com      # must print the reserved IP
   ```

---

## 8. Phase 3 — Host setup

Copy the kit to the instance and run the host script as `ubuntu`, not as root:

```bash
scp -i ~/.ssh/oci_devbox -r ~/devbox-kit ubuntu@$IP:~/
ssh -i ~/.ssh/oci_devbox ubuntu@$IP
bash ~/devbox-kit/devbox-host-setup.sh
```

What it does, in order (full script in Appendix B):

1. Updates the system, installs `git`, `curl`, `jq` and `openssl`, and sets the timezone.
2. Creates a 4 GB swap file with swappiness 10 — cheap insurance against the OOM killer taking the editor.
3. Installs Docker Engine from Docker's own repository, with the buildx and compose plugins, and adds `ubuntu` to the `docker` group.
4. Installs Node.js 22 LTS and the Dev Container CLI, used only as the image builder so that `features` in `devcontainer.json` are honoured.
5. Checks egress to the four endpoints door A depends on.
6. Creates `/srv/dev` and installs the environment files from the kit (section 18). It never overwrites a file you edited: a differing kit version lands next to it as `<file>.new`.
7. Creates the shared Docker network `devenv_edge`.
8. Generates the Git key `/srv/dev/secrets/git/id_ed25519`, fetches GitHub's host keys from its API over HTTPS, and routes `github.com` through that key for `ubuntu` on the host. Every project container mounts the same key read-only, so pushes work from inside the editors.
9. Installs fail2ban with the `sshd` jail, since port 22 is open to the internet.
10. Confirms ufw is inactive and leaves the host firewall unchanged.

**Why the host firewall is left alone.** Oracle's Ubuntu images load `/etc/iptables/rules.v4`: SSH allowed,
then a final `REJECT` on both `INPUT` and `FORWARD`. Traffic to a port that Docker publishes is DNAT-ed in
`PREROUTING` and crosses `FORWARD`, where Docker inserts its own accept rules ahead of that `REJECT`; it never
touches `INPUT`. Caddy's 80 and 443 therefore work without any host rule, and `INPUT` keeps protecting
everything else. Two things to avoid on this host: ufw (section 3), and `iptables-restore` or
`netfilter-persistent reload`/`save` while Docker runs. A restore flushes Docker's chains and breaks container
networking until Docker restarts; a save freezes Docker's and fail2ban's runtime rules into the file.

Then finish by hand:

1. **Log out and back in** so the `docker` group applies: `exit`, then `ssh` again.
2. **Add the Git key to GitHub.** Print it with `cat /srv/dev/secrets/git/id_ed25519.pub`, add it under GitHub → Settings → SSH and GPG keys → New SSH key, and check with `ssh -T git@github.com`. The key reaches every repository your account can; to limit that, add it instead as a deploy key with write access on each repository.
3. **Verify:**

   ```bash
   docker run --rm hello-world
   devcontainer --version
   dev                          # prints the usage
   ```

Final shape of `/srv/dev` once everything is built:

```
/srv/dev/
├── bin/dev                          management script (18.8)
├── config.env                       your settings (18.1)
├── compose.base.yml                 caddy + shared network (18.2)
├── caddy/
│   ├── Caddyfile                    global options + import (18.3)
│   ├── sites/<project>.caddy        generated per project
│   ├── sites/_desktop.caddy         optional desktop (section 12)
│   └── config/                      Caddy's autosaved configuration
├── images/entrypoint.sh             shared workspace entrypoint (18.4)
├── templates/
│   ├── devenv.compose.yml.tpl       (18.5)
│   ├── site.caddy.tpl               (18.6)
│   └── Dockerfile.tpl               (18.7)
├── secrets/
│   ├── <project>.password           generated, mode 0600
│   └── git/                         Git key + GitHub host keys, mounted read-only
├── projects/<project>/              git clone; contains .devcontainer/
│   └── devenv.compose.yml           generated service fragment
├── desktop/                         optional Guacamole stack (section 12)
└── backups/
```

Only `projects/` holds real work, and it is bind-mounted rather than copied so that an image rebuild never
touches source. Git remotes remain the actual backup; the bind mount is convenience, not durability. ACME
certificates live in the `caddy-data` Docker volume.

---

## 9. Phase 4 — Configure and start the edge

1. Set your values in `/srv/dev/config.env` (full file in section 18.1):

   ```bash
   nano /srv/dev/config.env      # BASE_DOMAIN, ACME_EMAIL; optionally GIT_NAME, GIT_EMAIL
   ```

2. Start the edge:

   ```bash
   dev up
   docker logs devenv-caddy
   ```

Caddy starts with no sites configured, which is expected; it obtains certificates lazily as sites appear.
`dev up` refuses to start while `BASE_DOMAIN` or `ACME_EMAIL` still hold placeholders: an empty email makes
the Caddyfile unparseable, and Caddy would restart-loop.

### Hardening door B

In descending order of value. Do at least the first two before relying on door B from untrusted networks.

1. **A long random password per project.** `dev add` generates one automatically.
2. **An identity layer in front**, so the code-server login page is never the outermost defence. Cloudflare Access is the least work; Authelia if you want it self-hosted; Caddy `basic_auth` as an absolute minimum:

   ```bash
   docker exec -it devenv-caddy caddy hash-password      # prompts, prints a bcrypt hash
   nano /srv/dev/caddy/sites/nova64.caddy
   dev reload
   ```

   ```
   nova64.dev.example.com {
   	encode zstd gzip
   	basic_auth {
   		dani <bcrypt-hash-from-above>
   	}
   	reverse_proxy devenv-nova64:8080
   }
   ```

The browser then asks for these credentials before code-server asks for its own password. To protect every future project the same way, put the block in `templates/site.caddy.tpl` (section 18.6).
3. **crowdsec or fail2ban reading the Caddy access log.** Enable Caddy's `log` directive first; it records no requests by default. With fail2ban, ban in Docker's `DOCKER-USER` chain as section 12 does, because this traffic never crosses `INPUT`.
4. **No inbound ports at all.** If you can accept it operationally, replace Caddy with a Cloudflare Tunnel (`cloudflared`) so door B is also outbound-only. Same container, same code-server, nothing published on the VM.

Never expose code-server on a published host port without TLS and authentication in front of it.

---

## 10. Phase 5 — First project and tunnel login

```bash
dev add nova64 git@github.com:youruser/nova64.git
```

This clones the repository, scaffolds `.devcontainer/` if the repo has none, generates the password, the
compose fragment and the Caddy site, builds the image through the Dev Container CLI so that `features` are
honoured, starts the container and reloads Caddy. `dev add nova64` without a URL starts an empty project.

If the repository already carries a `devcontainer.json`, it is used unchanged — but the image must contain the
two editor servers and the pre-created home directories from `Dockerfile.tpl` (section 18.7), or neither door
will start. Add those blocks to the project's own Dockerfile, keeping them architecture-aware as written: the
devbox is ARM64, GitHub Codespaces is x86-64, and the same file has to build on both.

The first build on two Ampere cores takes several minutes. After that the image is cached, and a container
start takes seconds. Rebuild after any change to `devcontainer.json` or the Dockerfile — this is your
equivalent of a Codespaces prebuild:

```bash
dev build nova64
```

### One-time tunnel login

```bash
dev login nova64
```

The logs print a device code. Open `https://github.com/login/device`, enter it, and authorise. Once the
tunnel reports it is connected, press Ctrl-C.

The tunnel CLI's data directory is pinned to the project's `-cli` volume, so this happens **once per
project**, not on every rebuild. Tunnel names are unique per GitHub account — if a name is rejected, release
it with `code tunnel unregister --name <name>` from any machine signed in to that account.

Then open:

- **Door A:** `https://vscode.dev/tunnel/nova64`
- **Door B:** `https://nova64.dev.example.com` — password from `dev add`, or `cat /srv/dev/secrets/nova64.password`

### Settings, extensions and Git

- **Settings Sync** works on door A — enable it there. code-server has no built-in Settings Sync: on door B, use the gist-based Settings Sync extension from Open VSX or keep your settings in a dotfiles repository. The two doors also keep separate extension stores.
- **Install both doors as PWAs.** In a normal tab the browser swallows `Ctrl+W`, `Ctrl+N` and `Ctrl+T`; an installed app window releases most of them. This is the single biggest quality-of-life difference in browser-based editing.
- **Git inside the container** commits as `GIT_NAME` / `GIT_EMAIL` from `config.env`, and pushes over SSH with the devbox key through `GIT_SSH_COMMAND`, which applies to git only. Use SSH remotes (`git@github.com:...`).

---

## 11. Phase 6 — Validation

Run these **from the most restrictive network you actually need this to work on**, not from home. The
terminal tests are the ones that matter, because the WebSocket is what inspecting proxies break.

- [ ] `dev status` shows caddy and the project container up
- [ ] Door A loads: `https://vscode.dev/tunnel/nova64`, file tree visible
- [ ] **Door A terminal opens and accepts input**
- [ ] Door B loads over valid TLS: `https://nova64.dev.example.com`
- [ ] **Door B terminal opens and accepts input**
- [ ] `git push` from a container terminal succeeds
- [ ] `dev restart nova64` → both doors return with no re-authentication
- [ ] `dev build nova64` → workspace files intact, no tunnel re-login
- [ ] `sudo reboot` → both doors return unattended
- [ ] A full toolchain build succeeds inside the container
- [ ] A saturating build leaves the editor usable (`dev status` in another window)
- [ ] `sudo ufw status` reports inactive; `sudo iptables -S INPUT` still ends with the image's `REJECT`
- [ ] `sudo fail2ban-client status sshd` shows the jail
- [ ] Settings Sync enabled on door A
- [ ] The 1 EUR budget alert exists
- [ ] `dev add` a throwaway second project, confirm it is up within a few minutes, then `dev remove` it

About the saturating build: both editor servers share the container's CPU quota with the build. The quota
protects the host and the proxy, not the editor, so a `make -j2` slows typing too. If the editor lags, run
heavy builds with `nice -n 10` or fewer parallel jobs.

---

## 12. Phase 7 (optional) — Web desktop with Guacamole

Not needed for the development environment. This phase adds a full Linux desktop in a browser tab, for the
GUI applications the editor doors cannot run — KiCad, a waveform viewer, a vendor GUI — and publishes it
through the same Caddy edge as door B at `https://desktop.<BASE_DOMAIN>`. No new port is opened.

### 12.1 What it adds

```
browser ── HTTPS 443 ──► devenv-caddy ──► guacamole :8080   webapp, TOTP, on devenv_edge
                                              │ 4822
                                            guacd ── RDP ──► xrdp :3389 on the host ──► XFCE session
                                              │
                                  PostgreSQL (users, connections, TOTP secrets)
```

Nothing new is exposed. Port 443 is already open. xrdp listens on `0.0.0.0:3389` because guacd reaches it
over the Docker bridge, but the host firewall accepts 3389 only from Docker's address range (`172.16.0.0/12`)
and the OCI security list does not open it. Guacamole is published only on the host's loopback, for health
checks and an SSH-tunnel fallback (`ssh -L 8080:127.0.0.1:8080 ...`).

The XFCE session, Tomcat and PostgreSQL together use roughly 1–1.5 GB of RAM — the reason `DEFAULT_MEM` is
8g rather than the 10g of the original runbook.

### 12.2 Install

Prerequisites, checked by the script: section 8 done, a real `BASE_DOMAIN` in `config.env`, and the edge up
(`dev up`). The wildcard record from section 7 already covers the desktop hostname.

```bash
bash ~/devbox-kit/devbox-desktop-setup.sh
```

The script prompts once for a password for the `ubuntu` account. xrdp authenticates through PAM and the
account ships with SSH-key authentication only. That password goes into the Guacamole RDP connection, not
into the web login. SSH stays key-only: the image disables password authentication for SSH.

What it does, in order (full script in Appendix C):

1. Installs XFCE, xrdp, xorgxrdp and theme packages
2. Writes a polkit rule for colord and packagekit
3. Removes screen lockers
4. Binds xrdp to `0.0.0.0` and starts it
5. Verifies the Guacamole images publish `arm64` manifests, then deploys guacd, the webapp and PostgreSQL in `/srv/dev/desktop`, with the webapp joined to `devenv_edge`
6. Accepts 3389 only from `172.16.0.0/12`, at runtime and in `/etc/iptables/rules.v4`
7. Writes `/srv/dev/caddy/sites/_desktop.caddy`, reloads Caddy and waits for the certificate
8. Adds a fail2ban jail for the Guacamole login that bans in Docker's `DOCKER-USER` chain
9. Verifies the listening sockets and the 3389 rule

### 12.3 First login and connection setup

Open `https://desktop.dev.example.com`.

**Log in with `guacadmin` / `guacadmin`.** These are Guacamole's own credentials, stored in PostgreSQL. The
`ubuntu` account and its password belong to the operating system and are used only inside the RDP connection
definition. Mixing these up is the most common stumbling block.

On first login, TOTP enrolment is presented as a QR code. Complete it, then immediately change the
guacadmin password: user menu → Settings → Preferences.

If you ever lose the TOTP device, clear the secret from the database:

```bash
cd /srv/dev/desktop
sudo docker compose exec postgres psql -U guacamole -d guacamole_db \
  -c "DELETE FROM guacamole_user_attribute WHERE attribute_name LIKE 'guac-totp%';"
```

**RDP connection:** Settings → Connections → New Connection.

| Field | Value |
|---|---|
| Name | devbox |
| Protocol | RDP |
| Hostname | `host.docker.internal` |
| Port | 3389 |
| Username | ubuntu |
| Password | the password set during the install |
| Security mode | Any |
| Ignore server certificate | enabled |

The hostname works because `extra_hosts: host.docker.internal:host-gateway` is declared on the **guacd**
service, not on the webapp. guacd is what opens the RDP socket; putting it on the wrong container produces a
generic "unable to connect" with no useful diagnostics. `Security mode: Any` matters because xrdp rejects NLA;
`Ignore server certificate` matters because xrdp presents a self-signed certificate.

Under Display:

| Setting | Value |
|---|---|
| Width / Height | **leave empty** |
| Resolution (DPI) | empty, or 120–144 on HiDPI screens |
| Color depth | 16-bit |
| Enable wallpaper | off |
| Enable font smoothing | off |
| Enable full window drag | off |

Leaving width and height empty is what lets the session adopt the browser window size. Any value there pins
the resolution and disables the adaptation. Display settings apply at session creation, so reopen the
connection after changing them.

### 12.4 Hardening and lockouts

- Five failed Guacamole logins within 10 minutes ban the source address for an hour on ports 80 and 443. The ban covers everything behind Caddy, so it also locks that address out of door B for the hour; door A is unaffected. Unban with `sudo fail2ban-client set guacamole unbanip <ip>`.
- To allow the desktop only from networks you trust without affecting door B, uncomment the two `remote_ip` lines in `/srv/dev/caddy/sites/_desktop.caddy`, set your CIDR, and run `dev reload`. Caddy sees real client addresses, because Docker's DNAT preserves them. (The source guide's advice to restrict 443 in the security list no longer fits: door B needs 443 from everywhere.)
- Re-running the desktop script keeps an edited `_desktop.caddy`.

### 12.5 Desktop appearance

The session runs XFCE, not GNOME. This is deliberate: GNOME depends on compositing and GPU acceleration,
neither of which works well over RDP, and on 2 ARM cores with no GPU the result is slow and visually broken.

XFCE's default theme looks dated but is easily changed. Settings live at Applications → Settings → Settings
Manager, or launch the panels directly:

```bash
xfce4-appearance-settings     # themes, icons, fonts
xfwm4-settings                # window decorations and compositor
xfce4-settings-manager        # full grid
```

Right-clicking the desktop background opens the full application menu, useful if the panel fails to start.

Recommended settings, with the theme packages already installed by the script:

- Appearance → Style: `Arc-Dark`; Icons: `Papirus-Dark`
- Window Manager → Style: `Arc-Dark`
- Window Manager Tweaks → Compositor: **disabled** — shadows and transparency are exactly what stalls over RDP

If you want something closer to a conventional modern desktop, MATE is a reasonable middle ground:

```bash
sudo apt install -y ubuntu-mate-desktop
echo "mate-session" > ~/.xsession
sudo pkill -u ubuntu xfce4-session
sudo systemctl restart xrdp
```

Avoid `ubuntu-desktop` (GNOME) on xrdp. It can be forced to run, but you will spend the time fighting the
compositor and Wayland, and half the RAM is gone before you open anything.

### 12.6 Troubleshooting the desktop

**Cannot log in as ubuntu on the web page.** That page is Guacamole: use `guacadmin` / `guacadmin`. If that
also fails, the schema was never loaded — PostgreSQL only runs init scripts when the data directory is empty,
so a first `up` with a missing or empty `initdb.sql` leaves a database with no tables, and the file is never
applied again.

```bash
cd /srv/dev/desktop
sudo docker compose exec postgres \
  psql -U guacamole -d guacamole_db -c "SELECT name FROM guacamole_entity;"
```

If that errors, rebuild:

```bash
wc -l initdb.sql        # several hundred lines, never 0
sudo docker compose down
sudo rm -rf pgdata
sudo docker compose up -d
```

**"Unable to connect" when opening the RDP connection.** Test from inside guacd, which is what actually dials
the socket:

```bash
sudo docker exec guacd nc -zv host.docker.internal 3389
sudo docker logs guacd --tail 30
```

A failure here is the host rule for 3389 (`sudo iptables -S INPUT | grep 3389`), not Guacamole.

**XFCE hangs on a colord authentication dialog.** The polkit rule is missing or in the wrong format. Ubuntu
24.04 uses the JavaScript `rules.d` format; the `.pkla` files in older guides have no effect.

**Resolution does not follow the browser window.** Width and height are set in the connection's Display
section — clear them. Then verify which X backend is serving the session:

```bash
ps aux | grep -E "Xorg|Xvnc" | grep -v grep
```

`Xvnc` means xrdp fell back to a VNC session, which has no dynamic resize. Fix the ordering in
`/etc/xrdp/xrdp.ini` so the Xorg channel comes first, and check `channel_code=1` is set. xrdp sessions persist
across connections, so closing the browser tab returns you to the same session with the same geometry. Force a
clean one:

```bash
sudo pkill -u ubuntu xfce4-session
```

**The desktop does not load, but door B does.** Check that the stack is up and that the webapp is attached
to the edge network:

```bash
sudo docker compose -f /srv/dev/desktop/docker-compose.yml ps
docker network inspect devenv_edge --format '{{range .Containers}}{{.Name}} {{end}}'   # must list guacamole
docker logs devenv-caddy --tail 50
```

**fail2ban does not ban.** Ubuntu 24.04 sends logs to the journal, and the jail reads the container's journal
entries. Check that the filter matches your Guacamole version's log format — it changed in 1.6:

```bash
sudo journalctl CONTAINER_NAME=guacamole --since "1 hour ago" > /tmp/guac.log
sudo fail2ban-regex /tmp/guac.log /etc/fail2ban/filter.d/guacamole.conf
sudo fail2ban-client status guacamole
```

Zero matches after a deliberate failed login means the regex needs adjusting to the strings actually present.

### 12.7 Maintenance and removal

**Guacamole upgrades.** Change the version tag in both image lines of `/srv/dev/desktop/docker-compose.yml`
(and `GUAC_VERSION` in the script, so a re-run does not revert it), then
`sudo docker compose pull && sudo docker compose up -d`. Major versions can require database schema
upgrades, shipped with the release and applied manually — read the release notes first — and may change the
log format the fail2ban filter relies on. Back up first:

```bash
cd /srv/dev/desktop
sudo docker compose exec postgres pg_dump -U guacamole guacamole_db > backup.sql
```

`dev backup` does not include the desktop: its database files belong to the PostgreSQL container's user.

**Removal**, leaving the development environment untouched:

```bash
cd /srv/dev/desktop && sudo docker compose down
rm /srv/dev/caddy/sites/_desktop.caddy && dev reload
sudo rm /etc/fail2ban/jail.d/guacamole.conf && sudo systemctl restart fail2ban
sudo iptables -D INPUT -s 172.16.0.0/12 -p tcp -m tcp --dport 3389 -j ACCEPT
sudo sed -i '/--dport 3389 -j ACCEPT/d' /etc/iptables/rules.v4
sudo systemctl disable --now xrdp
```

---

## 13. Daily operation

```bash
dev status                   # what is running, its limits, what it is using
dev logs nova64              # follow a container
dev shell nova64             # shell in, without a browser
dev restart nova64           # after a hung tunnel
dev build nova64             # after changing devcontainer.json
dev update nova64            # fresh code-server and VS Code CLI (rebuild without cache)
dev add other-project <url>  # new environment
dev down other-project       # dormant project: stop it, keep everything
```

Reboots need no action: `restart: unless-stopped` plus an enabled Docker service brings everything back,
tunnels included, with no re-authentication.

From a browser-only location, administration goes through Cloud Shell: `source ~/devbox.env && devbox`.

`DEFAULT_CPUS` and `DEFAULT_MEM` apply when a project is created. To change one existing project, edit
`cpus:` / `mem_limit:` in `/srv/dev/projects/<name>/devenv.compose.yml` and run `dev up <name>`; compose
recreates the container with the new limits.

---

## 14. Vendor and proprietary toolchains

Anything that cannot be installed from a package repository — licensed compilers, vendor FPGA suites,
proprietary SDKs — should not be baked into a public image or committed to the repository. Two workable
patterns:

1. **Mount the installer.** Add a read-only bind mount of a host directory holding the installer and licence file, and install from `postCreateCommand` on first create.
2. **Private base image.** Build a private image once with the vendor tool inside, push it to a private registry, and have `devcontainer.json` use it as the `FROM`.

**Architecture first.** The devbox is ARM64. Before planning around a vendor tool, confirm that it ships an
aarch64 Linux build; many do not. Legacy Xilinx ISE, needed for Spartan-6 targets, is x86-64 only: it does not
run on this host (qemu-user emulation aside, which is impractically slow for synthesis), and the open-source
`yosys` / `nextpnr` flow does not support Spartan-6 either. Keep that work on an x86 machine.

**Sizing.** Some vendor suites are very large — ISE is roughly 15–20 GB installed. Check free disk before
committing to putting one of these inside a container image, and prefer pattern 2 so the cost is paid once
rather than per rebuild. Command-line flows run headless, so where the architecture fits, it is a disk
problem, not a GUI problem.

---

## 15. Backup and maintenance

### Backup

```bash
dev backup                       # tar of configuration, secrets and source, mode 0600
```

The archive lands in `/srv/dev/backups/`, on the same boot volume: copy it off the instance (for example with
`scp` to Cloud Shell) if it has to survive losing the instance. For anything you actually care about, git
remotes are the real backup and this tarball is only for the environment itself. If you want proper snapshots
of the source tree, add restic against `/srv/dev/projects` on a timer:

```bash
restic -r <repo> backup /srv/dev/projects --exclude='**/node_modules' --exclude='**/.git/objects'
```

Not in `dev backup`, by design: the Docker images (rebuild with `dev build`), the named volumes (tunnel tokens
and extensions — one `dev login` per project; door A's extensions return through Settings Sync), the `caddy-data` volume
with the ACME certificates (losing it costs a re-issue, not an outage) and the desktop's database
(section 12.7).

OCI can also back up the whole boot volume on a schedule: Block Storage → Boot Volumes → the devbox's boot
volume → assign a backup policy. Check your tenancy's Always Free backup allowance before enabling one; the
budget alert is the backstop.

### Keeping software current

- **Operating system:** Ubuntu's unattended-upgrades applies security updates. Reboot after kernel updates; everything comes back unattended.
- **Editor servers:** `dev update <name>`. A plain `dev build` reuses the cached layer that downloaded code-server and the VS Code CLI, so it never picks up new versions. code-server faces the internet behind door B: update it regularly and when a security release is announced.
- **Base image:** `docker pull mcr.microsoft.com/devcontainers/base:bookworm` before `dev update` to refresh it too.
- **Caddy:** `docker pull caddy:2-alpine && dev up`; compose recreates the container when the image changed.
- **Guacamole:** section 12.7.

### Cost control

Keep the budget alert from section 5, and keep the total A1 allocation at 2 OCPU / 12 GB. Oracle has changed
the allowance without notice more than once.

---

## 16. Troubleshooting

Desktop-specific problems are covered in section 12.6.

| Symptom | Likely cause | Check / fix |
|---|---|---|
| Door A page loads, terminal never connects | WebSocket blocked or rewritten by a TLS-inspecting proxy | Try door B from the same network; if B works, that network is A-hostile |
| Device-code login requested after every rebuild | Tunnel CLI data outside the `-cli` volume | `docker exec devenv-<name> printenv VSCODE_CLI_DATA_DIR` must print `/home/vscode/.vscode-cli`; compare the fragment with section 18.5 |
| Container restart-loops on first start | Named volumes root-owned | The `mkdir`/`chown` block in the Dockerfile (18.7) is missing |
| Container exits: `unexpected value 'false' for '--random-name'` | Entrypoint from the old runbook | Use the entrypoint in 18.4 |
| `exec format error` during a build or at start | An x86-64 binary in an ARM64 image, e.g. `cli-alpine-x64` | Use the architecture-aware blocks in 18.7 |
| Tunnel name rejected | Name registered to the account, or longer than 20 characters | `code tunnel unregister --name <name>`; `dev add` enforces the length |
| `dev up` or `dev add` stops with "set BASE_DOMAIN" / "set ACME_EMAIL" | Placeholders still in `config.env` | Edit `/srv/dev/config.env` |
| Caddy restart-loops | Invalid Caddyfile or site file: empty email, duplicate hostname, typo | `docker logs devenv-caddy`; fix the file; `dev up` |
| Caddy cannot obtain a certificate | DNS not pointing at the IP, 80/443 missing from the security list, or a CAA record | `dig +short <host>`; section 6 ingress table; `docker logs devenv-caddy` |
| `ERR_SSL_PROTOCOL_ERROR` on `https://<ip>` | Expected: no certificate for a bare IP, and browsers send no SNI for IP literals | Use the hostname |
| `502` from door B | code-server not running, or container not on `devenv_edge` | `dev logs <name>`; `dev restart <name>` |
| `dev add` failed during the build | Toolchain or Dockerfile error | Fix the Dockerfile, `dev build <name>`, then `dev reload` (the site was written before the build) |
| Whole VM unresponsive during builds | Container limit too high | `dev status`; lower `cpus:` in the project's fragment, `dev up <name>` |
| Only the editor lags during builds | The editor shares the container's quota with the build | `nice -n 10`, fewer parallel jobs |
| Extension installs on door A but not B | Not published on Open VSX | Side-load the `.vsix`, or do that work on door A |
| Files owned by the wrong user in the workspace | Host UID ≠ container UID | `vscode` is UID 1000, same as `ubuntu` on Oracle's image; match the owner of `projects/<name>` |
| `dev` cannot reach Docker | Session predates the `docker` group | Log out and back in |
| `git push`: `Permission denied (publickey)` | Key not added to GitHub, or an HTTPS remote | `ssh -T git@github.com` on the host; `git remote -v`; use `git@github.com:` remotes |
| SSH lost after a firewall change | `rules.v4` edited wrongly, or ufw enabled | OCI Console → instance → Console connection gives serial access; it needs a local password (`sudo passwd ubuntu` in advance) or a GRUB single-user boot |
| Instance stopped without your action | Idle reclamation (Free Tier), or A1 limit enforcement | Start it; confirm the shape is 2 OCPU / 12 GB; consider Pay As You Go |

---

## 17. Known limitations

| Limitation | Impact | Mitigation |
|---|---|---|
| No USB passthrough from a cloud VM | Hardware debugging, device programming and any bring-up cannot run here | Keep the physical-hardware loop on the local workstation; this environment is for edit, build and synthesis |
| GUI applications | Not usable through the editor doors | Optional desktop (section 12) runs Linux GUI apps in a browser tab; heavy ones are sluggish (no GPU, RDP) |
| Dev Containers extension unusable from the web client | Cannot open a container *from* the browser session | Solved by design — the server runs inside the container |
| Open VSX gap on door B | Proprietary extensions missing when door A is blocked | Accept degraded tooling on the fallback path, or side-load `.vsix` |
| No built-in Settings Sync on door B | Settings do not follow you to code-server | Settings Sync extension from Open VSX, or a dotfiles repository |
| One tunnel per container | Cannot open two projects in one session | One browser tab (or PWA window) per project — the Codespaces model anyway |
| Browser keyboard shortcut conflicts | `Ctrl+W`, `Ctrl+N`, `Ctrl+T` intercepted | Install as a PWA |
| 2 OCPU ceiling | Single-threaded place-and-route or link steps cannot be parallelised; the editor shares the container's quota | Expect it; do not run two heavy jobs at once |
| ARM64 host | x86-64-only vendor binaries do not run | Keep them on an x86 machine (section 14) |
| Always Free terms change without notice | The A1 allowance was halved in 2026 | Budget alert; Pay As You Go |
| No auto-suspend | Containers idle at near-zero cost; the VM runs around the clock — free within Always Free, but reclaimable when idle on Free Tier accounts | `dev down <name>` for dormant projects; Pay As You Go |

---

## 18. Reference — environment files

`devbox-host-setup.sh` installs these files into `/srv/dev` (section 8). Edit them on the host; the kit
copies are only the starting point.

### 18.1 `/srv/dev/config.env`

The only file you must edit. Mode 0600.

<!-- file: srv/dev/config.env -->
```bash
# Base domain for door B. Projects are served at <project>.<BASE_DOMAIN>.
# Needs a wildcard DNS record *.<BASE_DOMAIN> pointing at the instance (section 7).
BASE_DOMAIN="dev.example.com"

# Contact email for the ACME (Let's Encrypt) account Caddy registers.
# Required: the Caddyfile does not parse with an empty value.
ACME_EMAIL="you@example.com"

# Default resource ceiling per project container. Host: OCI A1 Always Free,
# 2 OCPU / 12 GB.
# CPU: 1.5 leaves half a core to the host (Docker, Caddy, sshd), so a
# saturating build cannot starve the proxy or your way back in. Both editor
# servers run INSIDE the container and share its 1.5 cores with the build.
# Memory: a ceiling, not a reservation. 8g leaves ~4 GB to the host, the edge
# and the optional desktop stack (section 12).
# Both are copied into a project's compose fragment by `dev add`. To change an
# existing project, edit cpus/mem_limit in projects/<name>/devenv.compose.yml
# and run `dev up <name>`.
DEFAULT_CPUS="1.5"
DEFAULT_MEM="8g"

# Container timezone.
TZ="Europe/Madrid"

# Git identity applied inside every project container at start. Optional:
# leave empty to manage it yourself. Plain text, no double quotes.
GIT_NAME=""
GIT_EMAIL=""
```

### 18.2 `/srv/dev/compose.base.yml`

Caddy plus the shared network. Project service fragments are merged into it at runtime by `dev`, so this file
never needs editing when projects are added. The `edge` network is external: the optional desktop joins it,
and `dev down` must never delete it.

<!-- file: srv/dev/compose.base.yml -->
```yaml
name: devenv

networks:
  edge:
    # External: created by devbox-host-setup.sh (and on demand by `dev`), so
    # that `dev down` never tries to delete it while the optional desktop
    # stack (section 12) is still attached.
    name: devenv_edge
    external: true

volumes:
  caddy-data:

services:
  caddy:
    image: caddy:2-alpine
    container_name: devenv-caddy
    restart: unless-stopped
    ports:
      - "80:80"
      - "443:443"
    environment:
      ACME_EMAIL: "${ACME_EMAIL}"
    volumes:
      - /srv/dev/caddy/Caddyfile:/etc/caddy/Caddyfile:ro
      - /srv/dev/caddy/sites:/etc/caddy/sites:ro
      - caddy-data:/data
      - /srv/dev/caddy/config:/config
    networks: [edge]
```

### 18.3 `/srv/dev/caddy/Caddyfile`

The `sites/` directory is empty at first; `dev add` writes one file per project into it, and the optional
desktop adds `_desktop.caddy`. Caddy tolerates an empty glob. The file is bind-mounted on its own: if an editor
replaces it rather than rewriting it in place, restart Caddy (`docker restart devenv-caddy`) instead of
`dev reload`.

<!-- file: srv/dev/caddy/Caddyfile -->
```
{
	email {$ACME_EMAIL}
}

import /etc/caddy/sites/*.caddy
```

### 18.4 `/srv/dev/images/entrypoint.sh`

Runs inside every project container, under `tini` as PID 1 (`init: true`). code-server goes to the
background; the tunnel runs in the foreground, so container health tracks door A.

<!-- file: srv/dev/images/entrypoint.sh -->
```bash
#!/usr/bin/env bash
# Starts both editor doors inside a project container.
set -euo pipefail

: "${TUNNEL_NAME:?TUNNEL_NAME must be set}"
: "${WORKSPACE_DIR:=/workspaces}"

# --- Git identity (optional, from config.env through the compose fragment) --
# Re-applied at every start: ~/.gitconfig lives in the container layer and is
# lost on every rebuild.
if command -v git >/dev/null 2>&1; then
	if [[ -n "${GIT_NAME:-}" ]]; then git config --global user.name "$GIT_NAME"; fi
	if [[ -n "${GIT_EMAIL:-}" ]]; then git config --global user.email "$GIT_EMAIL"; fi
fi

# --- Door B: code-server -----------------------------------------------------
if command -v code-server >/dev/null 2>&1; then
	if [[ -f /run/secrets/code_server_password ]]; then
		PASSWORD="$(cat /run/secrets/code_server_password)"
		export PASSWORD
		auth_mode="password"
	else
		echo "WARNING: no password secret found; starting code-server without auth" >&2
		auth_mode="none"
	fi

	code-server \
		--bind-addr 0.0.0.0:8080 \
		--auth "${auth_mode}" \
		--disable-telemetry \
		--disable-update-check \
		"${WORKSPACE_DIR}" &
	code_server_pid=$!
	echo "code-server started (pid ${code_server_pid}) on :8080"
else
	echo "code-server not installed in this image; door B disabled" >&2
fi

# --- Door A: code tunnel (foreground) ----------------------------------------
# VSCODE_CLI_DATA_DIR (set in the compose fragment) keeps the tunnel's GitHub
# token and downloaded servers on the <project>-cli volume.
cd "${WORKSPACE_DIR}"
exec code tunnel \
	--accept-server-license-terms \
	--name "${TUNNEL_NAME}"
```

### 18.5 `/srv/dev/templates/devenv.compose.yml.tpl`

Rendered by `dev add` into `projects/<name>/devenv.compose.yml`. `__PLACEHOLDERS__` are filled once, at
creation; `${GIT_NAME}` / `${GIT_EMAIL}` are interpolated by compose on every `dev` call.

<!-- file: srv/dev/templates/devenv.compose.yml.tpl -->
```yaml
volumes:
  __NAME__-cli:
  __NAME__-server:
  __NAME__-codeserver:

secrets:
  code_server_password___NAME__:
    file: /srv/dev/secrets/__NAME__.password

services:
  devenv-__NAME__:
    image: devenv/__NAME__:latest
    # Local image only: if it were ever missing, compose would otherwise try
    # to pull docker.io/devenv/__NAME__, a namespace that is not ours.
    pull_policy: never
    container_name: devenv-__NAME__
    hostname: __NAME__
    restart: unless-stopped
    init: true
    user: vscode
    working_dir: /workspaces/__NAME__
    command: ["/usr/local/bin/entrypoint.sh"]
    environment:
      TUNNEL_NAME: "__NAME__"
      WORKSPACE_DIR: "/workspaces/__NAME__"
      TZ: "__TZ__"
      # Pins the tunnel CLI's data dir (GitHub token, downloaded servers) to the
      # -cli volume. Without it the CLI migrates to ~/.vscode/cli, which lives
      # in the container layer and is lost on every rebuild.
      VSCODE_CLI_DATA_DIR: "/home/vscode/.vscode-cli"
      # Git: identity from config.env (interpolated by `dev`); SSH key shared
      # read-only from the host.
      GIT_NAME: "${GIT_NAME}"
      GIT_EMAIL: "${GIT_EMAIL}"
      GIT_SSH_COMMAND: "ssh -i /run/devbox-git/id_ed25519 -o IdentitiesOnly=yes -o UserKnownHostsFile=/run/devbox-git/known_hosts"
    volumes:
      - /srv/dev/projects/__NAME__:/workspaces/__NAME__
      - /srv/dev/images/entrypoint.sh:/usr/local/bin/entrypoint.sh:ro
      - /srv/dev/secrets/git:/run/devbox-git:ro
      - __NAME__-cli:/home/vscode/.vscode-cli
      - __NAME__-server:/home/vscode/.vscode-server
      - __NAME__-codeserver:/home/vscode/.local/share/code-server
    secrets:
      - source: code_server_password___NAME__
        target: code_server_password
    cpus: __CPUS__
    mem_limit: __MEM__
    stop_grace_period: 30s
    networks: [edge]
```

The three named volumes matter for different reasons. `-cli` holds the tunnel's GitHub token and the
downloaded VS Code servers — but only because `VSCODE_CLI_DATA_DIR` points there: the current CLI defaults to
`~/.vscode/cli`, which would live in the container layer and vanish on every rebuild. `-server` holds the
tunnel server's extensions and state. `-codeserver` holds door B's extensions and state separately, because
the two doors do not share an extension store.

### 18.6 `/srv/dev/templates/site.caddy.tpl`

<!-- file: srv/dev/templates/site.caddy.tpl -->
```
__NAME__.__BASE_DOMAIN__ {
	encode zstd gzip
	reverse_proxy devenv-__NAME__:8080
}
```

### 18.7 `/srv/dev/templates/Dockerfile.tpl`

Used only when a project has no `.devcontainer/` of its own. Projects that already have one keep it — the
whole point is that the same file still works on GitHub Codespaces.

<!-- file: srv/dev/templates/Dockerfile.tpl -->
```dockerfile
FROM mcr.microsoft.com/devcontainers/base:bookworm

# --- Project toolchain: edit this line per project ---------------------------
RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
	build-essential git make cmake ninja-build python3 python3-pip \
	&& rm -rf /var/lib/apt/lists/*

# --- Editor servers (self-hosted requirement, not needed on Codespaces) ------
# The VS Code CLI Alpine build is statically linked and runs on glibc distros
# too. The architecture is detected at build time, so the same file builds on
# the ARM64 devbox and on x86-64 (GitHub Codespaces, a local machine).
RUN case "$(uname -m)" in \
		x86_64)  cli_os=cli-alpine-x64 ;; \
		aarch64) cli_os=cli-alpine-arm64 ;; \
		*) echo "unsupported architecture: $(uname -m)" >&2; exit 1 ;; \
	esac \
	&& curl -fsSL "https://code.visualstudio.com/sha/download?build=stable&os=${cli_os}" \
		-o /tmp/vscode-cli.tar.gz \
	&& tar -xf /tmp/vscode-cli.tar.gz -C /usr/local/bin \
	&& rm /tmp/vscode-cli.tar.gz \
	&& curl -fsSL https://code-server.dev/install.sh | sh

# --- Pre-create the volume mount points owned by the runtime user ------------
# A fresh named volume inherits ownership from the image directory it covers.
# Without this the volumes come up root-owned and both doors fail to start.
RUN mkdir -p /home/vscode/.vscode-cli \
		/home/vscode/.vscode-server \
		/home/vscode/.local/share/code-server \
	&& chown -R vscode:vscode /home/vscode
```

If you prefer to keep the repository Dockerfile strictly portable, move the two self-hosted blocks into a
second Dockerfile stage referenced only by this environment. The cost of not doing so is roughly 200 MB of
image that Codespaces would not need.

### 18.8 `/srv/dev/bin/dev`

Installed at `/srv/dev/bin/dev` and linked as `/usr/local/bin/dev`.

<!-- file: srv/dev/bin/dev -->
```bash
#!/usr/bin/env bash
# dev — manage the self-hosted development environment
set -euo pipefail

DEV_ROOT="${DEV_ROOT:-/srv/dev}"
CONFIG="${DEV_ROOT}/config.env"
EDGE_NETWORK="devenv_edge"

# shellcheck disable=SC1090
[[ -f "$CONFIG" ]] && source "$CONFIG"

BASE_DOMAIN="${BASE_DOMAIN:-}"
ACME_EMAIL="${ACME_EMAIL:-}"
DEFAULT_CPUS="${DEFAULT_CPUS:-1.5}"
DEFAULT_MEM="${DEFAULT_MEM:-8g}"
TZ="${TZ:-UTC}"
GIT_NAME="${GIT_NAME:-}"
GIT_EMAIL="${GIT_EMAIL:-}"

# Read by docker compose when it interpolates compose.base.yml and the fragments.
export ACME_EMAIL GIT_NAME GIT_EMAIL

die()  { echo "error: $*" >&2; exit 1; }
info() { echo "==> $*"; }

# Refuse placeholders: an empty ACME_EMAIL makes the Caddyfile unparseable and
# Caddy restart-loops; a placeholder domain can never get a certificate.
check_config() {
	[[ -n "$BASE_DOMAIN" && "$BASE_DOMAIN" != "dev.example.com" ]] \
		|| die "set BASE_DOMAIN in ${CONFIG}"
	[[ -n "$ACME_EMAIL" && "$ACME_EMAIL" != "you@example.com" ]] \
		|| die "set ACME_EMAIL in ${CONFIG}"
}

# The edge network is external to every compose project; create it on demand.
ensure_network() {
	docker network inspect "$EDGE_NETWORK" >/dev/null 2>&1 \
		|| docker network create "$EDGE_NETWORK" >/dev/null
}

# Build the -f list: base file plus every project fragment that exists.
compose() {
	ensure_network
	local args=(-f "${DEV_ROOT}/compose.base.yml")
	local f
	for f in "${DEV_ROOT}"/projects/*/devenv.compose.yml; do
		[[ -e "$f" ]] && args+=(-f "$f")
	done
	docker compose --project-directory "${DEV_ROOT}" "${args[@]}" "$@"
}

require_project() {
	local name="$1"
	[[ -n "$name" ]] || die "project name required"
	[[ -d "${DEV_ROOT}/projects/${name}" ]] || die "unknown project: ${name}"
}

render() {
	# render <template> <name>
	sed -e "s|__NAME__|$2|g" \
		-e "s|__BASE_DOMAIN__|${BASE_DOMAIN}|g" \
		-e "s|__CPUS__|${DEFAULT_CPUS}|g" \
		-e "s|__MEM__|${DEFAULT_MEM}|g" \
		-e "s|__TZ__|${TZ}|g" \
		"$1"
}

cmd_add() {
	local name="${1:-}" repo="${2:-}"
	[[ -n "$name" ]] || die "usage: dev add <name> [git-url]"
	[[ "$name" =~ ^[a-z0-9][a-z0-9-]*$ ]] || die "name must be lowercase alphanumeric with hyphens"
	(( ${#name} <= 20 )) || die "name must be at most 20 characters (VS Code tunnel name limit)"
	check_config
	local dir="${DEV_ROOT}/projects/${name}"
	[[ -e "$dir" ]] && die "project already exists: ${dir}"
	# Two site blocks for one hostname make Caddy reject the whole configuration.
	if grep -rqsF "${name}.${BASE_DOMAIN} {" "${DEV_ROOT}/caddy/sites/"; then
		die "hostname ${name}.${BASE_DOMAIN} is already served by another site file"
	fi

	if [[ -n "$repo" ]]; then
		info "cloning ${repo}"
		git clone "$repo" "$dir"
	else
		mkdir -p "$dir"
	fi

	if [[ ! -f "${dir}/.devcontainer/devcontainer.json" ]]; then
		info "no devcontainer.json found — scaffolding one"
		mkdir -p "${dir}/.devcontainer"
		cat > "${dir}/.devcontainer/devcontainer.json" <<JSON
{
  "name": "${name}",
  "build": { "dockerfile": "Dockerfile" },
  "features": {
    "ghcr.io/devcontainers/features/common-utils:2": {
      "username": "vscode",
      "installZsh": true
    },
    "ghcr.io/devcontainers/features/git:1": {}
  },
  "remoteUser": "vscode"
}
JSON
		cp "${DEV_ROOT}/templates/Dockerfile.tpl" "${dir}/.devcontainer/Dockerfile"
		info "edit ${dir}/.devcontainer/Dockerfile to add the project toolchain"
	fi

	info "generating password secret"
	( umask 077; openssl rand -base64 24 > "${DEV_ROOT}/secrets/${name}.password" )

	info "generating compose fragment"
	render "${DEV_ROOT}/templates/devenv.compose.yml.tpl" "$name" > "${dir}/devenv.compose.yml"

	info "generating caddy site"
	render "${DEV_ROOT}/templates/site.caddy.tpl" "$name" > "${DEV_ROOT}/caddy/sites/${name}.caddy"

	cmd_build "$name"
	cmd_reload

	echo
	info "project '${name}' created"
	echo "    door A: https://vscode.dev/tunnel/${name}   (one-time login: dev login ${name})"
	echo "    door B: https://${name}.${BASE_DOMAIN}"
	echo "    password: $(cat "${DEV_ROOT}/secrets/${name}.password")"
}

cmd_build() {
	local name="${1:-}"
	shift || true
	require_project "$name"
	info "building image devenv/${name}:latest"
	devcontainer build \
		--workspace-folder "${DEV_ROOT}/projects/${name}" \
		--image-name "devenv/${name}:latest" \
		"$@"
	info "recreating container"
	compose up -d --force-recreate "devenv-${name}"
}

# Rebuild ignoring the layer cache: pulls the current code-server, VS Code CLI
# and apt packages. A cached `dev build` keeps whatever was downloaded first.
cmd_update()  { cmd_build "${1:-}" --no-cache; }

cmd_up() {
	if [[ -n "${1:-}" ]]; then
		require_project "$1"
		compose up -d "devenv-$1"
	else
		check_config
		compose up -d
	fi
}
cmd_down()    { if [[ -n "${1:-}" ]]; then require_project "$1"; compose stop "devenv-$1"; else compose down; fi; }
cmd_restart() { require_project "${1:-}"; compose restart "devenv-$1"; }
cmd_logs()    { require_project "${1:-}"; compose logs -f --tail=100 "devenv-$1"; }
cmd_shell()   { require_project "${1:-}"; compose exec "devenv-$1" bash; }

cmd_reload() {
	info "reloading caddy"
	compose exec -T -w /etc/caddy caddy caddy reload --config /etc/caddy/Caddyfile \
		|| echo "warning: caddy reload failed — check: docker logs devenv-caddy" >&2
}

cmd_login() {
	local name="${1:-}"
	require_project "$name"
	echo "Watch for the device code below, then open https://github.com/login/device"
	echo "Press Ctrl-C once the tunnel reports it is connected."
	compose logs -f --tail=50 "devenv-${name}"
}

cmd_status() {
	compose ps
	echo
	printf '%-20s %-5s %-5s %s\n' PROJECT CPUS MEM "DOOR A | DOOR B"
	local d name frag cpus mem
	for d in "${DEV_ROOT}"/projects/*/; do
		frag="${d}devenv.compose.yml"
		[[ -f "$frag" ]] || continue
		name="$(basename "$d")"
		# Actual limits live in each fragment; config.env only seeds new projects.
		cpus="$(awk '$1 == "cpus:" { print $2; exit }' "$frag")"
		mem="$(awk '$1 == "mem_limit:" { print $2; exit }' "$frag")"
		printf '%-20s %-5s %-5s %s\n' "$name" "${cpus:-?}" "${mem:-?}" \
			"https://vscode.dev/tunnel/${name} | https://${name}.${BASE_DOMAIN}"
	done
	echo
	docker stats --no-stream --format 'table {{.Name}}\t{{.CPUPerc}}\t{{.MemUsage}}' || true
}

cmd_remove() {
	local name="${1:-}"
	require_project "$name"
	read -rp "Remove project '${name}'? Source in projects/${name} is KEPT. [y/N] " a
	[[ "$a" == "y" ]] || exit 0
	compose rm -sf "devenv-${name}" || true
	docker volume rm "devenv_${name}-cli" "devenv_${name}-server" "devenv_${name}-codeserver" 2>/dev/null || true
	rm -f "${DEV_ROOT}/caddy/sites/${name}.caddy" \
		"${DEV_ROOT}/projects/${name}/devenv.compose.yml" \
		"${DEV_ROOT}/secrets/${name}.password"
	cmd_reload
	info "removed '${name}'; source left at ${DEV_ROOT}/projects/${name}"
	info "release the tunnel name with: code tunnel unregister --name ${name}"
}

cmd_backup() {
	local stamp; stamp="$(date +%Y%m%d-%H%M%S)"
	local out="${DEV_ROOT}/backups/devenv-${stamp}.tar.gz"
	umask 077   # the archive contains secrets
	info "writing ${out}"
	tar czf "$out" \
		--exclude='*/node_modules' \
		--exclude='*/.git/objects' \
		--exclude="${DEV_ROOT#/}/backups" \
		-C / "${DEV_ROOT#/}/config.env" "${DEV_ROOT#/}/compose.base.yml" \
		"${DEV_ROOT#/}/caddy/Caddyfile" "${DEV_ROOT#/}/caddy/sites" \
		"${DEV_ROOT#/}/images" "${DEV_ROOT#/}/templates" \
		"${DEV_ROOT#/}/secrets" "${DEV_ROOT#/}/bin" "${DEV_ROOT#/}/projects"
	info "done"
}

usage() {
	cat <<'TXT'
dev — self-hosted development environment

  dev add <name> [git-url]   create a project, build it and bring it up
  dev build <name>           rebuild the image (cached) and recreate the container
  dev update <name>          rebuild without cache: fresh editor servers and packages
  dev up [name]              start everything, or one project
  dev down [name]            stop everything, or one project
  dev restart <name>         restart one project
  dev logs <name>            follow container logs
  dev login <name>           show the one-time tunnel device-code login
  dev shell <name>           open a shell inside the container
  dev status                 containers, limits and live resource usage
  dev reload                 reload caddy configuration
  dev remove <name>          remove a project (source is kept)
  dev backup                 tar the whole configuration and source tree
TXT
}

case "${1:-}" in
	add)     shift; cmd_add "$@" ;;
	build)   shift; cmd_build "$@" ;;
	update)  shift; cmd_update "${1:-}" ;;
	up)      shift; cmd_up "${1:-}" ;;
	down)    shift; cmd_down "${1:-}" ;;
	restart) shift; cmd_restart "${1:-}" ;;
	logs)    shift; cmd_logs "${1:-}" ;;
	login)   shift; cmd_login "${1:-}" ;;
	shell)   shift; cmd_shell "${1:-}" ;;
	status)  shift; cmd_status ;;
	reload)  shift; cmd_reload ;;
	remove)  shift; cmd_remove "${1:-}" ;;
	backup)  shift; cmd_backup ;;
	*)       usage; exit 1 ;;
esac
```

---

## Appendix A — devbox-oci-setup.sh

Run from Cloud Shell or a workstation with the OCI CLI. Creates compartment, network, security list rules,
SSH key and instance. Section 6.

<!-- file: devbox-oci-setup.sh -->
```bash
#!/usr/bin/env bash
#
# devbox-oci-setup.sh
#
# Creates the complete OCI (Always Free) infrastructure for the devbox:
#   compartment -> VCN -> internet gateway -> route -> public subnet
#   -> security list ingress (80/443) -> SSH key -> instance
#
# Region:  your tenancy HOME region (Always Free compute exists only there)
# Shape:   VM.Standard.A1.Flex (Ampere ARM), 2 OCPU / 12 GB -> Always Free
# Image:   Canonical Ubuntu 24.04 aarch64
# Boot:    100 GB, default VPU (Balanced) -> Always Free
#
# Idempotent: every resource is looked up by name first and reused if it
# exists. Re-run it as often as needed, which is normal while fighting
# "Out of host capacity", and re-run it to refresh ~/devbox.env after the
# public IP changes (section 7).
#
# Usage:
#   bash devbox-oci-setup.sh
#   OPEN_WEB=false bash devbox-oci-setup.sh     # do not touch the security list
#
# Requires: OCI CLI configured (oci setup config), or run it from Cloud Shell.
#

set -euo pipefail

# ---------------------------------------------------------------------------
# Configuration
# ---------------------------------------------------------------------------

REGION="${REGION:-eu-frankfurt-1}"   # must be the tenancy home region (checked below)

COMPARTMENT_NAME="devbox"
VCN_NAME="vcn-devbox"
VCN_CIDR="10.0.0.0/16"
VCN_DNS_LABEL="vcndevbox"
IGW_NAME="igw-devbox"
SUBNET_NAME="subnet-devbox-public"
SUBNET_CIDR="10.0.0.0/24"
SUBNET_DNS_LABEL="public"

INSTANCE_NAME="devbox"
SHAPE="VM.Standard.A1.Flex"
OCPUS=2
MEMORY_GB=12
BOOT_VOLUME_GB=100
OS_NAME="Canonical Ubuntu"
OS_VERSION="24.04"
SSH_USER="ubuntu"                     # 'opc' if you switch to Oracle Linux

SSH_KEY="${HOME}/.ssh/oci_devbox"
ENV_FILE="${HOME}/devbox.env"

# Retries on OutOfHostCapacity
RETRY_ENABLED=true
RETRY_SLEEP=60                        # seconds between rounds; do not go below 60
RETRY_MAX_ROUNDS=0                    # 0 = unlimited

# Security list ingress for the Caddy edge (door B, optional desktop).
# Safe to open before anything listens: the Ubuntu image's host firewall
# rejects everything except SSH until Docker publishes Caddy's ports.
OPEN_WEB="${OPEN_WEB:-true}"
HTTP_SOURCE_CIDR="0.0.0.0/0"          # ACME HTTP-01 validation comes from anywhere
HTTPS_SOURCE_CIDR="0.0.0.0/0"         # door B must be reachable from every network you work from

export OCI_CLI_REGION="$REGION"

# ---------------------------------------------------------------------------
# Utilities
# ---------------------------------------------------------------------------

RED=$'\033[0;31m'; GRN=$'\033[0;32m'; YLW=$'\033[0;33m'; BLU=$'\033[0;34m'; RST=$'\033[0m'

log()  { printf '%s[%s]%s %s\n' "$BLU" "$(date +%H:%M:%S)" "$RST" "$*"; }
ok()   { printf '%s  ok%s    %s\n' "$GRN" "$RST" "$*"; }
warn() { printf '%s  warn%s  %s\n' "$YLW" "$RST" "$*"; }
die()  { printf '%s  error%s %s\n' "$RED" "$RST" "$*" >&2; exit 1; }

# Runs an OCI query that may return nothing; normalises null -> ""
ociq() {
  local out
  out="$("$@" 2>/dev/null || true)"
  [[ "$out" == "null" ]] && out=""
  printf '%s' "$out"
}

# ---------------------------------------------------------------------------
# 0. Pre-flight checks
# ---------------------------------------------------------------------------

command -v oci >/dev/null 2>&1 || die "oci CLI not found. Install it or use Cloud Shell."
command -v python3 >/dev/null 2>&1 || die "python3 not found (needed to edit the security list)."

log "Checking credentials and region ${REGION}"
# Cloud Shell exports OCI_TENANCY; elsewhere derive it from the compartment list.
TENANCY="${OCI_TENANCY:-}"
if [[ -z "$TENANCY" ]]; then
  TENANCY="$(ociq oci iam compartment list --access-level ACCESSIBLE --limit 1 \
              --query 'data[0]."compartment-id"' --raw-output)"
fi
[[ -n "$TENANCY" ]] || die "Could not determine the tenancy. Check ~/.oci/config."
ok "tenancy: ${TENANCY}"

HOME_REGION="$(ociq oci iam region-subscription list --tenancy-id "$TENANCY" \
                --query 'data[?"is-home-region"] | [0]."region-name"' --raw-output)"
if [[ -n "$HOME_REGION" && "$HOME_REGION" != "$REGION" ]]; then
  die "REGION=${REGION} is not the tenancy home region (${HOME_REGION}). Always Free A1 instances exist only in the home region: re-run with REGION=${HOME_REGION}."
fi
[[ -n "$HOME_REGION" ]] && ok "home region: ${HOME_REGION}"

# Real Ampere core quota available
AVAIL_CORES="$(ociq oci limits resource-availability get \
                 --service-name compute \
                 --limit-name standard-a1-core-count \
                 --compartment-id "$TENANCY" \
                 --availability-domain "$(ociq oci iam availability-domain list \
                     --compartment-id "$TENANCY" --query 'data[0].name' --raw-output)" \
                 --query 'data.available' --raw-output)"
if [[ -n "$AVAIL_CORES" ]]; then
  ok "Ampere A1 cores available per quota: ${AVAIL_CORES}"
  if [[ "${AVAIL_CORES%.*}" -lt "$OCPUS" ]]; then
    warn "the quota (${AVAIL_CORES}) is lower than the ${OCPUS} OCPUs requested."
    warn "check whether another A1 instance is already consuming the allowance."
  fi
fi

# ---------------------------------------------------------------------------
# 1. Compartment
# ---------------------------------------------------------------------------

log "Compartment '${COMPARTMENT_NAME}'"
C="$(ociq oci iam compartment list --compartment-id "$TENANCY" --all \
      --query "data[?name=='${COMPARTMENT_NAME}' && \"lifecycle-state\"=='ACTIVE'].id | [0]" \
      --raw-output)"

if [[ -n "$C" ]]; then
  ok "exists: ${C}"
else
  C="$(oci iam compartment create \
        --compartment-id "$TENANCY" \
        --name "$COMPARTMENT_NAME" \
        --description "Self-hosted development environment" \
        --wait-for-state ACTIVE \
        --query 'data.id' --raw-output)"
  ok "created: ${C}"

  # IAM takes a few seconds to propagate a new compartment
  log "waiting for IAM propagation"
  for _ in $(seq 1 30); do
    ociq oci network vcn list --compartment-id "$C" --query 'data' >/dev/null && break
    sleep 3
  done
fi

# ---------------------------------------------------------------------------
# 2. VCN
# ---------------------------------------------------------------------------

log "VCN '${VCN_NAME}'"
VCN="$(ociq oci network vcn list --compartment-id "$C" --display-name "$VCN_NAME" \
        --lifecycle-state AVAILABLE --query 'data[0].id' --raw-output)"

if [[ -n "$VCN" ]]; then
  ok "exists: ${VCN}"
else
  VCN="$(oci network vcn create \
          --compartment-id "$C" \
          --display-name "$VCN_NAME" \
          --cidr-blocks "[\"${VCN_CIDR}\"]" \
          --dns-label "$VCN_DNS_LABEL" \
          --wait-for-state AVAILABLE \
          --query 'data.id' --raw-output)"
  ok "created: ${VCN}"
fi

RT="$(ociq oci network vcn get --vcn-id "$VCN" \
       --query 'data."default-route-table-id"' --raw-output)"
SL="$(ociq oci network vcn get --vcn-id "$VCN" \
       --query 'data."default-security-list-id"' --raw-output)"
[[ -n "$RT" && -n "$SL" ]] || die "Could not read the VCN's route table / security list."

# ---------------------------------------------------------------------------
# 3. Internet gateway + default route
# ---------------------------------------------------------------------------

log "Internet gateway '${IGW_NAME}'"
IGW="$(ociq oci network internet-gateway list --compartment-id "$C" --vcn-id "$VCN" \
        --query 'data[0].id' --raw-output)"

if [[ -n "$IGW" ]]; then
  ok "exists: ${IGW}"
else
  IGW="$(oci network internet-gateway create \
          --compartment-id "$C" \
          --vcn-id "$VCN" \
          --display-name "$IGW_NAME" \
          --is-enabled true \
          --wait-for-state AVAILABLE \
          --query 'data.id' --raw-output)"
  ok "created: ${IGW}"
fi

log "Default route 0.0.0.0/0 -> IGW"
CURRENT_ROUTE="$(ociq oci network route-table get --rt-id "$RT" \
                  --query "data.\"route-rules\"[?destination=='0.0.0.0/0'] | [0].\"network-entity-id\"" \
                  --raw-output)"
if [[ "$CURRENT_ROUTE" == "$IGW" ]]; then
  ok "already configured"
else
  # CAUTION: --force REPLACES every rule. The default route table of a freshly
  # created VCN is empty, so nothing is lost here.
  oci network route-table update --rt-id "$RT" --force \
    --route-rules "[{\"destination\":\"0.0.0.0/0\",\"destinationType\":\"CIDR_BLOCK\",\"networkEntityId\":\"${IGW}\"}]" \
    >/dev/null
  ok "updated"
fi

# ---------------------------------------------------------------------------
# 4. Public subnet
# ---------------------------------------------------------------------------

log "Subnet '${SUBNET_NAME}'"
SUBNET="$(ociq oci network subnet list --compartment-id "$C" --vcn-id "$VCN" \
           --display-name "$SUBNET_NAME" --lifecycle-state AVAILABLE \
           --query 'data[0].id' --raw-output)"

if [[ -n "$SUBNET" ]]; then
  ok "exists: ${SUBNET}"
else
  SUBNET="$(oci network subnet create \
             --compartment-id "$C" \
             --vcn-id "$VCN" \
             --display-name "$SUBNET_NAME" \
             --cidr-block "$SUBNET_CIDR" \
             --dns-label "$SUBNET_DNS_LABEL" \
             --route-table-id "$RT" \
             --security-list-ids "[\"${SL}\"]" \
             --prohibit-public-ip-on-vnic false \
             --wait-for-state AVAILABLE \
             --query 'data.id' --raw-output)"
  ok "created: ${SUBNET}"
fi

# The default security list already allows TCP 22 + ICMP. Enough for SSH.

# ---------------------------------------------------------------------------
# 5. Security list: ingress 80/443 for the Caddy edge
# ---------------------------------------------------------------------------

if [[ "$OPEN_WEB" == "true" ]]; then
  log "Security list ingress: TCP 80 from ${HTTP_SOURCE_CIDR}, TCP 443 from ${HTTPS_SOURCE_CIDR}"
  EXISTING="$(oci network security-list get --security-list-id "$SL" \
               --query 'data."ingress-security-rules"' --output json)"
  # The update API replaces the whole list, so merge: keep every existing rule
  # (converted from the CLI's kebab-case output to the camelCase the update
  # expects) and append only the ports that are missing.
  MERGED="$(python3 - "$EXISTING" "$HTTP_SOURCE_CIDR" "$HTTPS_SOURCE_CIDR" <<'PY'
import json, sys
existing = json.loads(sys.argv[1] or "[]")
wanted = {80: sys.argv[2], 443: sys.argv[3]}

def conv(r):
    out = {}
    for k, v in r.items():
        if v is None:
            continue
        ck = ''.join(w if i == 0 else w.capitalize() for i, w in enumerate(k.split('-')))
        out[ck] = conv(v) if isinstance(v, dict) else v
    return out

def has(port):
    for r in existing:
        rng = (r.get("tcp-options") or {}).get("destination-port-range") or {}
        if r.get("protocol") == "6" and rng.get("min") == port and rng.get("max") == port:
            return True
    return False

missing = [p for p in sorted(wanted) if not has(p)]
if missing:
    rules = [conv(r) for r in existing]
    for p in missing:
        rules.append({"protocol": "6", "source": wanted[p], "sourceType": "CIDR_BLOCK",
                      "isStateless": False,
                      "tcpOptions": {"destinationPortRange": {"min": p, "max": p}}})
    print(json.dumps(rules))
PY
)"
  if [[ -z "$MERGED" ]]; then
    ok "rules for 80 and 443 already present"
  else
    oci network security-list update --security-list-id "$SL" --force \
      --ingress-security-rules "$MERGED" >/dev/null
    ok "ingress rules added"
  fi
else
  warn "OPEN_WEB=false: security list left untouched (door B and the desktop need 80/443)"
fi

# ---------------------------------------------------------------------------
# 6. SSH key
# ---------------------------------------------------------------------------

log "SSH key"
if [[ -f "${SSH_KEY}.pub" ]]; then
  ok "exists: ${SSH_KEY}.pub"
else
  mkdir -p "$(dirname "$SSH_KEY")"
  chmod 700 "$(dirname "$SSH_KEY")"
  ssh-keygen -t ed25519 -C "devbox-oci" -f "$SSH_KEY" -N "" >/dev/null
  ok "generated: ${SSH_KEY}"
fi

# ---------------------------------------------------------------------------
# 7. Image and availability domains
# ---------------------------------------------------------------------------

log "Image ${OS_NAME} ${OS_VERSION} for ${SHAPE}"
IMG="$(ociq oci compute image list \
        --compartment-id "$C" \
        --operating-system "$OS_NAME" \
        --operating-system-version "$OS_VERSION" \
        --shape "$SHAPE" \
        --sort-by TIMECREATED --sort-order DESC \
        --query 'data[0].id' --raw-output)"
[[ -n "$IMG" ]] || die "No ${OS_NAME} ${OS_VERSION} image compatible with ${SHAPE} found."

IMG_NAME="$(ociq oci compute image get --image-id "$IMG" \
             --query 'data."display-name"' --raw-output)"
ok "${IMG_NAME}"
[[ "$IMG_NAME" == *aarch64* ]] || warn "the image does not look like aarch64; check it before continuing."

mapfile -t ADS < <(oci iam availability-domain list --compartment-id "$C" \
                     --query 'data[*].name' --raw-output | tr -d '[],"' | sed '/^\s*$/d' | sed 's/^ *//')
[[ ${#ADS[@]} -gt 0 ]] || die "Could not list the availability domains."
ok "availability domains: ${#ADS[@]}"

# ---------------------------------------------------------------------------
# 8. Instance
# ---------------------------------------------------------------------------

log "Instance '${INSTANCE_NAME}'"
# Any state except TERMINATED/TERMINATING counts as "exists": a re-run while a
# previous launch is still PROVISIONING, or after a stop, must not launch a twin.
read -r INST INST_STATE < <(ociq oci compute instance list --compartment-id "$C" \
         --display-name "$INSTANCE_NAME" \
         --query "data[?\"lifecycle-state\"!='TERMINATED' && \"lifecycle-state\"!='TERMINATING'] | [0].[id, \"lifecycle-state\"] | join(' ', @)" \
         --raw-output; echo) || true

if [[ -n "${INST:-}" ]]; then
  ok "exists: ${INST} (${INST_STATE})"
  case "$INST_STATE" in
    RUNNING) ;;
    STOPPED)
      log "starting the stopped instance"
      oci compute instance action --instance-id "$INST" --action START \
        --wait-for-state RUNNING >/dev/null
      ok "RUNNING" ;;
    PROVISIONING|STARTING|MOVING|CREATING_IMAGE)
      log "waiting for RUNNING"
      for _ in $(seq 1 60); do
        [[ "$(ociq oci compute instance get --instance-id "$INST" \
               --query 'data."lifecycle-state"' --raw-output)" == "RUNNING" ]] && break
        sleep 10
      done ;;
    *) die "instance is ${INST_STATE}; wait for it to settle and re-run." ;;
  esac
else
  INST=""
  ERRFILE="$(mktemp)"
  trap 'rm -f "$ERRFILE"' EXIT

  launch_one() {
    local ad="$1"
    oci compute instance launch \
      --compartment-id "$C" \
      --availability-domain "$ad" \
      --display-name "$INSTANCE_NAME" \
      --shape "$SHAPE" \
      --shape-config "{\"ocpus\":${OCPUS},\"memoryInGBs\":${MEMORY_GB}}" \
      --image-id "$IMG" \
      --subnet-id "$SUBNET" \
      --assign-public-ip true \
      --boot-volume-size-in-gbs "$BOOT_VOLUME_GB" \
      --ssh-authorized-keys-file "${SSH_KEY}.pub" \
      --wait-for-state RUNNING \
      --query 'data.id' --raw-output 2>"$ERRFILE"
  }

  round=0
  while true; do
    round=$((round + 1))
    for ad in "${ADS[@]}"; do
      log "launching in ${ad} (round ${round})"
      if INST="$(launch_one "$ad")" && [[ -n "$INST" ]]; then
        ok "instance RUNNING in ${ad}: ${INST}"
        break 2
      fi

      # A failure that is NOT about capacity is a real error: stop.
      if ! grep -qi 'OutOfHostCapacity\|Out of host capacity' "$ERRFILE"; then
        echo "---" >&2
        cat "$ERRFILE" >&2
        die "Failure not related to capacity. Read the message above."
      fi
      warn "no capacity in ${ad}"
    done

    [[ "$RETRY_ENABLED" == "true" ]] || die "No capacity in any AD and retries are disabled."
    if [[ "$RETRY_MAX_ROUNDS" -gt 0 && "$round" -ge "$RETRY_MAX_ROUNDS" ]]; then
      die "Gave up after ${RETRY_MAX_ROUNDS} rounds without capacity."
    fi
    log "waiting ${RETRY_SLEEP}s before the next round"
    sleep "$RETRY_SLEEP"
  done
fi

# ---------------------------------------------------------------------------
# 9. Public IP and final state
# ---------------------------------------------------------------------------

log "Reading the public IP"
IP=""
for _ in $(seq 1 20); do
  IP="$(ociq oci compute instance list-vnics --instance-id "$INST" \
         --query 'data[0]."public-ip"' --raw-output)"
  [[ -n "$IP" ]] && break
  sleep 5
done
[[ -n "$IP" ]] || die "The instance is running but has no public IP assigned."
ok "public IP: ${IP}"

cat > "$ENV_FILE" <<EOF
# Generated by devbox-oci-setup.sh on $(date -Iseconds)
export OCI_CLI_REGION=${REGION}
export TENANCY=${TENANCY}
export C=${C}
export VCN=${VCN}
export RT=${RT}
export SL=${SL}
export IGW=${IGW}
export SUBNET=${SUBNET}
export IMG=${IMG}
export INST=${INST}
export IP=${IP}
export SSH_KEY=${SSH_KEY}
export SSH_USER=${SSH_USER}
alias devbox='ssh -i ${SSH_KEY} ${SSH_USER}@${IP}'
EOF
ok "state saved to ${ENV_FILE}"

log "Waiting for port 22"
for _ in $(seq 1 40); do
  if ssh -i "$SSH_KEY" -o StrictHostKeyChecking=accept-new \
         -o ConnectTimeout=5 -o BatchMode=yes \
         "${SSH_USER}@${IP}" true 2>/dev/null; then
    ok "SSH is up"
    break
  fi
  sleep 10
done

cat <<EOF

${GRN}Infrastructure ready.${RST}

  compartment  ${COMPARTMENT_NAME}      ${C}
  vcn          ${VCN_NAME}   ${VCN}
  subnet       ${SUBNET_NAME}
  instance     ${INSTANCE_NAME}         ${INST}
  shape        ${SHAPE}  ${OCPUS} OCPU / ${MEMORY_GB} GB
  boot volume  ${BOOT_VOLUME_GB} GB (default VPU)
  public IP    ${IP}
  ingress      22 (default)$( [[ "$OPEN_WEB" == "true" ]] && echo ", 80, 443" )

Connect:
  ssh -i ${SSH_KEY} ${SSH_USER}@${IP}

Reload these variables in another session:
  source ${ENV_FILE}

Next step: section 7 — reserve the public IP, then point DNS at it.

${YLW}Reminder:${RST} create a 1 EUR budget with an alert at 1% on the root
compartment. Oracle has changed the A1 limits without notice more than
once; this way you find out within hours instead of at month end.
EOF
```

---

## Appendix B — devbox-host-setup.sh

Run on the instance as `ubuntu`, from the extracted kit. Section 8.

<!-- file: devbox-host-setup.sh -->
```bash
#!/usr/bin/env bash
#
# devbox-host-setup.sh
#
# Prepares the OCI instance to host the self-hosted development environment:
#
#   base packages -> swap -> Docker Engine -> Node.js + Dev Container CLI
#   -> egress checks -> /srv/dev layout and files -> shared edge network
#   -> GitHub SSH key -> fail2ban (sshd)
#
# Run it as the 'ubuntu' user (not root), from the extracted kit:
#
#   bash ~/devbox-kit/devbox-host-setup.sh
#
# Idempotent: safe to re-run. Files under /srv/dev are never overwritten; if
# the kit version differs from yours it is written alongside as <file>.new.
#

set -euo pipefail

# ---------------------------------------------------------------------------
# Configuration
# ---------------------------------------------------------------------------

DEV_ROOT="/srv/dev"
EDGE_NETWORK="devenv_edge"
TIMEZONE="Europe/Madrid"
SWAP_SIZE="4G"
NODE_MAJOR="22"                       # Node.js LTS line; the Dev Container CLI needs >= 20
KIT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# ---------------------------------------------------------------------------
# Utilities
# ---------------------------------------------------------------------------

RED=$'\033[0;31m'; GRN=$'\033[0;32m'; YLW=$'\033[0;33m'; BLU=$'\033[0;34m'; RST=$'\033[0m'
log()  { printf '\n%s==>%s %s\n' "$BLU" "$RST" "$*"; }
ok()   { printf '%s  ok%s    %s\n' "$GRN" "$RST" "$*"; }
warn() { printf '%s  warn%s  %s\n' "$YLW" "$RST" "$*"; }
die()  { printf '%s  error%s %s\n' "$RED" "$RST" "$*" >&2; exit 1; }

apt_install() {
  sudo DEBIAN_FRONTEND=noninteractive NEEDRESTART_MODE=a apt-get install -y -qq \
    -o Dpkg::Options::=--force-confdef -o Dpkg::Options::=--force-confold "$@"
}

# install_file <path relative to the kit's srv/dev> <mode>
# Installs a kit file into DEV_ROOT without ever clobbering a file you edited.
install_file() {
  local rel="$1" mode="$2"
  local src="${KIT_DIR}/srv/dev/${rel}" dst="${DEV_ROOT}/${rel}"
  [[ -f "$src" ]] || die "missing in the kit: ${src}"
  if [[ ! -e "$dst" ]]; then
    install -D -m "$mode" "$src" "$dst"
    ok "installed ${dst}"
  elif cmp -s "$src" "$dst"; then
    ok "${dst} (unchanged)"
  else
    install -m "$mode" "$src" "${dst}.new"
    warn "${dst} differs from the kit: kept yours, kit version at ${dst}.new"
  fi
}

[[ $EUID -ne 0 ]] || die "Do not run this as root. Use the 'ubuntu' user."
sudo -v || die "sudo is required."

# ---------------------------------------------------------------------------
# 0. Pre-flight checks
# ---------------------------------------------------------------------------

log "Pre-flight checks"

ARCH="$(dpkg --print-architecture)"
[[ "$ARCH" == "arm64" ]] || warn "architecture ${ARCH}; this runbook targets arm64 (OCI Ampere A1)"
ok "architecture: ${ARCH}"

# shellcheck disable=SC1091
. /etc/os-release
[[ "${ID:-}" == "ubuntu" ]] || warn "written for Ubuntu; found ${PRETTY_NAME:-unknown}"
ok "os: ${PRETTY_NAME:-unknown}"

[[ -f "${KIT_DIR}/srv/dev/bin/dev" ]] \
  || die "kit files not found under ${KIT_DIR}/srv/dev. Extract the kit first (section 4)."
ok "kit: ${KIT_DIR}"

# ---------------------------------------------------------------------------
# 1. Base packages and timezone
# ---------------------------------------------------------------------------

log "Base packages"
sudo apt-get update -qq
sudo DEBIAN_FRONTEND=noninteractive NEEDRESTART_MODE=a apt-get upgrade -y -qq \
  -o Dpkg::Options::=--force-confdef -o Dpkg::Options::=--force-confold
apt_install ca-certificates curl gnupg git jq openssl lsb-release
ok "packages installed"

sudo timedatectl set-timezone "$TIMEZONE"
ok "timezone: ${TIMEZONE}"

# ---------------------------------------------------------------------------
# 2. Swap: cheap insurance against the OOM killer taking the editor
# ---------------------------------------------------------------------------

log "Swap (${SWAP_SIZE})"
if swapon --show=NAME --noheadings | grep -qx /swapfile; then
  ok "/swapfile already active"
else
  [[ -f /swapfile ]] || sudo fallocate -l "$SWAP_SIZE" /swapfile
  sudo chmod 600 /swapfile
  sudo mkswap /swapfile >/dev/null
  sudo swapon /swapfile
  ok "/swapfile active"
fi
grep -q '^/swapfile ' /etc/fstab || echo '/swapfile none swap sw 0 0' | sudo tee -a /etc/fstab >/dev/null
echo 'vm.swappiness=10' | sudo tee /etc/sysctl.d/99-swappiness.conf >/dev/null
sudo sysctl --system >/dev/null
ok "swappiness 10, persisted in /etc/fstab"

# ---------------------------------------------------------------------------
# 3. Docker Engine from the official repository (not the distro package)
# ---------------------------------------------------------------------------

log "Docker Engine"
if ! command -v docker >/dev/null 2>&1; then
  sudo install -m 0755 -d /etc/apt/keyrings
  curl -fsSL https://download.docker.com/linux/ubuntu/gpg \
    | sudo gpg --dearmor --yes -o /etc/apt/keyrings/docker.gpg
  sudo chmod a+r /etc/apt/keyrings/docker.gpg
  echo "deb [arch=${ARCH} signed-by=/etc/apt/keyrings/docker.gpg] https://download.docker.com/linux/ubuntu $(lsb_release -cs) stable" \
    | sudo tee /etc/apt/sources.list.d/docker.list >/dev/null
  sudo apt-get update -qq
  apt_install docker-ce docker-ce-cli containerd.io docker-buildx-plugin docker-compose-plugin
  ok "docker installed"
else
  ok "docker already installed"
fi
sudo systemctl enable --now docker >/dev/null
sudo usermod -aG docker "$USER"
ok "$(sudo docker --version)"

# ---------------------------------------------------------------------------
# 4. Node.js + the Dev Container CLI (used only as the image builder)
# ---------------------------------------------------------------------------

log "Node.js ${NODE_MAJOR}.x and the Dev Container CLI"
if ! command -v node >/dev/null 2>&1 || (( $(node -p 'process.versions.node.split(".")[0]') < 20 )); then
  curl -fsSL "https://deb.nodesource.com/setup_${NODE_MAJOR}.x" | sudo -E bash - >/dev/null
  apt_install nodejs
fi
ok "node $(node --version)"
if ! command -v devcontainer >/dev/null 2>&1; then
  sudo npm install -g --silent @devcontainers/cli
fi
ok "devcontainer CLI $(devcontainer --version)"

# ---------------------------------------------------------------------------
# 5. Egress: door A dies if the VM cannot reach these
# ---------------------------------------------------------------------------

log "Egress checks"
for url in https://vscode.dev https://update.code.visualstudio.com \
           https://global.rel.tunnels.api.visualstudio.com https://github.com; do
  code="$(curl -sS -o /dev/null -m 10 -w '%{http_code}' "$url" 2>/dev/null || true)"
  if [[ -n "$code" && "$code" != "000" ]]; then
    ok "${url} -> HTTP ${code}"
  else
    warn "${url} -> no answer; outbound 443 is filtered and door A will not work"
  fi
done

# ---------------------------------------------------------------------------
# 6. /srv/dev layout and environment files
# ---------------------------------------------------------------------------

log "Directory layout ${DEV_ROOT}"
sudo mkdir -p "${DEV_ROOT}"/{bin,caddy/{sites,config},images,projects,secrets/git,templates,backups}
# Non-recursive on purpose: never walk project trees on a re-run.
sudo chown "$USER:$USER" "$DEV_ROOT" "${DEV_ROOT}"/{bin,caddy,caddy/sites,caddy/config,images,projects,secrets,secrets/git,templates,backups}
chmod 700 "${DEV_ROOT}/secrets" "${DEV_ROOT}/secrets/git"
ok "layout ready"

log "Environment files from the kit"
install_file config.env                     600
install_file compose.base.yml               644
install_file caddy/Caddyfile                644
install_file images/entrypoint.sh           755
install_file templates/devenv.compose.yml.tpl 644
install_file templates/site.caddy.tpl       644
install_file templates/Dockerfile.tpl       644
install_file bin/dev                        755
sudo ln -sf "${DEV_ROOT}/bin/dev" /usr/local/bin/dev
ok "/usr/local/bin/dev -> ${DEV_ROOT}/bin/dev"

# ---------------------------------------------------------------------------
# 7. Shared edge network (external to every compose project)
# ---------------------------------------------------------------------------

log "Docker network '${EDGE_NETWORK}'"
if sudo docker network inspect "$EDGE_NETWORK" >/dev/null 2>&1; then
  ok "exists"
else
  sudo docker network create "$EDGE_NETWORK" >/dev/null
  ok "created"
fi

# ---------------------------------------------------------------------------
# 8. GitHub SSH key: clone on the host, push from inside the containers
# ---------------------------------------------------------------------------

log "GitHub SSH key"
GIT_KEYS="${DEV_ROOT}/secrets/git"
if [[ -f "${GIT_KEYS}/id_ed25519" ]]; then
  ok "key exists: ${GIT_KEYS}/id_ed25519"
else
  ssh-keygen -t ed25519 -C "devbox@$(hostname)" -f "${GIT_KEYS}/id_ed25519" -N "" >/dev/null
  ok "key generated: ${GIT_KEYS}/id_ed25519"
fi

# Host keys from GitHub's API over HTTPS, instead of trusting ssh-keyscan.
if curl -fsSL https://api.github.com/meta | jq -r '.ssh_keys[] | "github.com " + .' \
     > "${GIT_KEYS}/known_hosts.tmp" && [[ -s "${GIT_KEYS}/known_hosts.tmp" ]]; then
  mv "${GIT_KEYS}/known_hosts.tmp" "${GIT_KEYS}/known_hosts"
  ok "known_hosts from api.github.com/meta"
else
  rm -f "${GIT_KEYS}/known_hosts.tmp"
  warn "could not fetch GitHub host keys; re-run later (git over SSH will prompt otherwise)"
fi
chmod 600 "${GIT_KEYS}/id_ed25519"
chmod 644 "${GIT_KEYS}/id_ed25519.pub"
[[ -f "${GIT_KEYS}/known_hosts" ]] && chmod 644 "${GIT_KEYS}/known_hosts"

mkdir -p "${HOME}/.ssh" && chmod 700 "${HOME}/.ssh"
if grep -qs '^# devbox: github' "${HOME}/.ssh/config"; then
  ok "${HOME}/.ssh/config already routes github.com through the devbox key"
else
  cat >> "${HOME}/.ssh/config" <<EOF

# devbox: github (added by devbox-host-setup.sh)
Host github.com
    IdentityFile ${GIT_KEYS}/id_ed25519
    IdentitiesOnly yes
    UserKnownHostsFile ${GIT_KEYS}/known_hosts
EOF
  chmod 600 "${HOME}/.ssh/config"
  ok "${HOME}/.ssh/config: github.com uses the devbox key"
fi

# ---------------------------------------------------------------------------
# 9. fail2ban for SSH (port 22 is open to the internet)
# ---------------------------------------------------------------------------

log "fail2ban (sshd)"
apt_install fail2ban
# Ubuntu 24.04 logs authentication to the journal, not /var/log/auth.log.
sudo tee /etc/fail2ban/jail.d/00-backend.conf >/dev/null <<'EOF'
[DEFAULT]
backend = systemd
EOF
sudo systemctl enable fail2ban >/dev/null
sudo systemctl restart fail2ban
sleep 2
if sudo fail2ban-client status sshd >/dev/null 2>&1; then
  ok "sshd jail active"
else
  warn "sshd jail not active: journalctl -u fail2ban -n 30"
fi

# ---------------------------------------------------------------------------
# 10. Host firewall: nothing to open, and no ufw
# ---------------------------------------------------------------------------

log "Host firewall"
ok "no change needed: Docker publishes Caddy's 80/443 through the FORWARD chain it manages"
if command -v ufw >/dev/null 2>&1 && sudo ufw status 2>/dev/null | grep -q 'Status: active'; then
  warn "ufw is ACTIVE. Oracle warns it can leave OCI instances unable to boot: sudo ufw disable"
else
  ok "ufw inactive (keep it that way on OCI)"
fi

# ---------------------------------------------------------------------------
# Summary
# ---------------------------------------------------------------------------

cat <<EOF

${GRN}Host ready.${RST}

Next steps:

  1. Log out and back in, so the docker group applies to your session:
       exit
       ssh -i ~/.ssh/oci_devbox ubuntu@<ip>

  2. Add this public key to GitHub (Settings -> SSH and GPG keys -> New SSH key):
       $(cat "${GIT_KEYS}/id_ed25519.pub")
     Then check it:  ssh -T git@github.com

  3. Edit ${DEV_ROOT}/config.env: BASE_DOMAIN, ACME_EMAIL, GIT_NAME, GIT_EMAIL

  4. Start the edge:  dev up
EOF
```

---

## Appendix C — devbox-desktop-setup.sh

Optional. Run on the instance as `ubuntu`, after section 9. Section 12.

<!-- file: devbox-desktop-setup.sh -->
```bash
#!/usr/bin/env bash
#
# devbox-desktop-setup.sh   (OPTIONAL — section 12)
#
# Adds a browser-accessible Linux desktop to the devbox, published through the
# SAME Caddy edge that serves door B:
#
#   XFCE + xrdp  ->  Guacamole (guacd + webapp + PostgreSQL)
#                ->  devenv-caddy  ->  https://desktop.<BASE_DOMAIN>
#
# Prerequisites: devbox-host-setup.sh has run, /srv/dev/config.env holds a real
# BASE_DOMAIN, and the edge is up (`dev up`). The wildcard DNS record from
# section 7 already covers the desktop hostname.
#
# Run as the 'ubuntu' user (not root). Idempotent: safe to re-run.
#

set -euo pipefail

# ---------------------------------------------------------------------------
# Configuration
# ---------------------------------------------------------------------------

DEV_ROOT="/srv/dev"
GUAC_VERSION="1.6.0"
GUAC_DIR="${DEV_ROOT}/desktop"
DESKTOP_SUBDOMAIN="desktop"          # served at https://<this>.<BASE_DOMAIN>
DOCKER_NET_CIDR="172.16.0.0/12"      # sources allowed to reach xrdp on 3389
EDGE_NETWORK="devenv_edge"
CADDY_CONTAINER="devenv-caddy"
RULES_FILE="/etc/iptables/rules.v4"

# ---------------------------------------------------------------------------
# Utilities
# ---------------------------------------------------------------------------

RED=$'\033[0;31m'; GRN=$'\033[0;32m'; YLW=$'\033[0;33m'; BLU=$'\033[0;34m'; RST=$'\033[0m'
log()  { printf '\n%s==>%s %s\n' "$BLU" "$RST" "$*"; }
ok()   { printf '%s  ok%s    %s\n' "$GRN" "$RST" "$*"; }
warn() { printf '%s  warn%s  %s\n' "$YLW" "$RST" "$*"; }
die()  { printf '%s  error%s %s\n' "$RED" "$RST" "$*" >&2; exit 1; }

apt_install() {
  sudo DEBIAN_FRONTEND=noninteractive NEEDRESTART_MODE=a apt-get install -y -qq \
    -o Dpkg::Options::=--force-confdef -o Dpkg::Options::=--force-confold "$@"
}

[[ $EUID -ne 0 ]] || die "Do not run this as root. Use the 'ubuntu' user."
sudo -v || die "sudo is required."

# ---------------------------------------------------------------------------
# 0. Pre-flight checks
# ---------------------------------------------------------------------------

log "Pre-flight checks"

ARCH="$(dpkg --print-architecture)"
ok "architecture: ${ARCH}"

[[ -f "${DEV_ROOT}/config.env" ]] || die "${DEV_ROOT}/config.env not found. Run devbox-host-setup.sh first."
# shellcheck disable=SC1091
source "${DEV_ROOT}/config.env"
[[ -n "${BASE_DOMAIN:-}" && "$BASE_DOMAIN" != "dev.example.com" ]] \
  || die "set BASE_DOMAIN in ${DEV_ROOT}/config.env first"
TIMEZONE="${TZ:-UTC}"
DESKTOP_HOST="${DESKTOP_SUBDOMAIN}.${BASE_DOMAIN}"

command -v docker >/dev/null 2>&1 || die "Docker not found. Run devbox-host-setup.sh first."
sudo docker network inspect "$EDGE_NETWORK" >/dev/null 2>&1 \
  || die "network ${EDGE_NETWORK} not found. Run 'dev up' first."
sudo docker ps --format '{{.Names}}' | grep -qx "$CADDY_CONTAINER" \
  || die "${CADDY_CONTAINER} is not running. Run 'dev up' first."
[[ ! -d "${DEV_ROOT}/projects/${DESKTOP_SUBDOMAIN}" ]] \
  || die "project '${DESKTOP_SUBDOMAIN}' already owns ${DESKTOP_HOST}; change DESKTOP_SUBDOMAIN"
ok "desktop will be served at https://${DESKTOP_HOST}"

# ---------------------------------------------------------------------------
# 1. XFCE desktop + xrdp
# ---------------------------------------------------------------------------

log "Installing XFCE and xrdp"

sudo apt-get update -qq
sudo DEBIAN_FRONTEND=noninteractive NEEDRESTART_MODE=a apt-get upgrade -y -qq \
  -o Dpkg::Options::=--force-confdef -o Dpkg::Options::=--force-confold
apt_install xfce4 xfce4-goodies dbus-x11 xrdp xorgxrdp \
  arc-theme papirus-icon-theme fonts-noto
sudo adduser xrdp ssl-cert >/dev/null 2>&1 || true
ok "packages installed"

echo "xfce4-session" > "${HOME}/.xsession"
ok "${HOME}/.xsession -> xfce4-session"

# polkit: without this, XFCE hangs asking for authentication for the colour
# manager. Ubuntu 24.04 uses the JavaScript rules.d format, not the old .pkla.
sudo tee /etc/polkit-1/rules.d/49-nopasswd-colord.rules > /dev/null <<'EOF'
polkit.addRule(function(action, subject) {
    if ((action.id.indexOf("org.freedesktop.color-manager.") === 0 ||
         action.id.indexOf("org.freedesktop.packagekit.") === 0) &&
        subject.isInGroup("sudo")) {
        return polkit.Result.YES;
    }
});
EOF
ok "polkit rule for colord/packagekit"

sudo apt-get remove -y -qq light-locker xscreensaver >/dev/null 2>&1 || true

# xrdp listens on 0.0.0.0 because the guacd container reaches it through the
# Docker bridge, not loopback. Step 3 restricts it with iptables.
sudo sed -i 's/^address=.*/address=0.0.0.0/' /etc/xrdp/xrdp.ini
grep -q '^address=' /etc/xrdp/xrdp.ini || \
  sudo sed -i '/^\[Globals\]/a address=0.0.0.0' /etc/xrdp/xrdp.ini

sudo systemctl enable --now xrdp >/dev/null
sudo systemctl restart xrdp
systemctl is-active --quiet xrdp || die "xrdp did not start. Check: journalctl -u xrdp"
ok "xrdp active on 3389"

# User password: xrdp authenticates through PAM and 'ubuntu' ships without one.
# (SSH stays key-only: the image disables SSH password authentication.)
if sudo passwd -S ubuntu | awk '{print $2}' | grep -qE '^(NP|L)$'; then
  log "User 'ubuntu' has no password and xrdp needs one"
  sudo passwd ubuntu
else
  ok "user 'ubuntu' already has a password"
fi

# ---------------------------------------------------------------------------
# 2. Guacamole
# ---------------------------------------------------------------------------

log "Deploying Guacamole ${GUAC_VERSION} in ${GUAC_DIR}"

mkdir -p "$GUAC_DIR"
cd "$GUAC_DIR"

# Check the official images publish arm64 before pulling them.
if [[ "$ARCH" == "arm64" ]]; then
  if sudo docker manifest inspect "guacamole/guacd:${GUAC_VERSION}" 2>/dev/null \
       | grep -q '"architecture": *"arm64"'; then
    ok "official images support arm64"
  else
    die "guacamole/guacd:${GUAC_VERSION} does not publish arm64. Use the
        all-in-one multi-arch image flcontainers/guacamole instead."
  fi
fi

# The schema must exist BEFORE the first 'up', or Docker creates an empty
# directory where it expects a file.
if [[ ! -s initdb.sql ]]; then
  # The redirect runs as 'ubuntu' on purpose: the file must be owned by us.
  # shellcheck disable=SC2024
  sudo docker run --rm "guacamole/guacamole:${GUAC_VERSION}" \
    /opt/guacamole/bin/initdb.sh --postgresql > initdb.sql
  [[ -s initdb.sql ]] || die "initdb.sql came out empty."
  ok "schema generated ($(wc -l < initdb.sql) lines)"
else
  ok "initdb.sql already exists"
fi

if [[ ! -f .env ]]; then
  ( umask 077; printf 'DB_PASSWORD=%s\n' "$(openssl rand -base64 24 | tr -d '/+=')" > .env )
  ok "database password generated in .env"
else
  ok ".env already exists"
fi

cat > docker-compose.yml <<EOF
name: desktop

networks:
  edge:
    name: ${EDGE_NETWORK}
    external: true

services:
  postgres:
    image: postgres:16-alpine
    container_name: guac-postgres
    environment:
      POSTGRES_DB: guacamole_db
      POSTGRES_USER: guacamole
      POSTGRES_PASSWORD: \${DB_PASSWORD}
    volumes:
      - ./pgdata:/var/lib/postgresql/data
      - ./initdb.sql:/docker-entrypoint-initdb.d/initdb.sql:ro
    restart: unless-stopped

  guacd:
    image: guacamole/guacd:${GUAC_VERSION}
    container_name: guacd
    # host-gateway goes HERE: guacd is what opens the RDP socket to xrdp, not
    # the webapp. On the wrong container you get a generic "unable to
    # connect" that is very hard to diagnose.
    extra_hosts:
      - "host.docker.internal:host-gateway"
    restart: unless-stopped

  guacamole:
    image: guacamole/guacamole:${GUAC_VERSION}
    container_name: guacamole
    depends_on:
      - postgres
      - guacd
    environment:
      GUACD_HOSTNAME: guacd
      GUACD_PORT: 4822
      POSTGRESQL_HOSTNAME: postgres
      POSTGRESQL_PORT: 5432
      POSTGRESQL_DATABASE: guacamole_db
      POSTGRESQL_USER: guacamole
      POSTGRESQL_PASSWORD: \${DB_PASSWORD}
      POSTGRESQL_AUTO_CREATE_ACCOUNTS: "false"
      TOTP_ENABLED: "true"
      WEBAPP_CONTEXT: ROOT
      TZ: "${TIMEZONE}"
      # Without this Tomcat sees Caddy's address instead of the client's, and
      # fail2ban would ban the proxy itself.
      REMOTE_IP_VALVE_ENABLED: "true"
    # 'edge' is how devenv-caddy reaches this container (reverse_proxy
    # guacamole:8080). The loopback port is only for health checks and an
    # SSH-tunnel fallback; nothing is published publicly.
    networks: [default, edge]
    ports:
      - "127.0.0.1:8080:8080"
    # journald so fail2ban can read failed logins without depending on the
    # container ID.
    logging:
      driver: journald
    restart: unless-stopped
EOF

sudo docker compose up -d
ok "containers up"

log "Waiting for Guacamole"
for i in $(seq 1 60); do
  if curl -sf -o /dev/null -w '%{http_code}' http://127.0.0.1:8080 | grep -qE '^(200|302)$'; then
    ok "Guacamole answers on 127.0.0.1:8080"
    break
  fi
  [[ $i -eq 60 ]] && die "Guacamole does not answer. Check: sudo docker compose -f ${GUAC_DIR}/docker-compose.yml logs"
  sleep 3
done

if sudo docker compose logs guacamole 2>/dev/null | grep -qi totp; then
  ok "TOTP extension loaded"
else
  warn "TOTP extension not seen in the log; check it after the first login"
fi

# ---------------------------------------------------------------------------
# 3. Host firewall: 3389 only from the Docker networks
# ---------------------------------------------------------------------------

log "Host firewall: xrdp reachable only from ${DOCKER_NET_CIDR}"

# The rule, in iptables-save form so it can be matched in the rules file.
RULE_SPEC=(-s "$DOCKER_NET_CIDR" -p tcp -m tcp --dport 3389 -j ACCEPT)
RULE_LINE="-A INPUT ${RULE_SPEC[*]}"

# Runtime: insert it ahead of the image's final REJECT.
if sudo iptables -C INPUT "${RULE_SPEC[@]}" 2>/dev/null; then
  ok "runtime rule present"
else
  sudo iptables -I INPUT 1 "${RULE_SPEC[@]}"
  ok "runtime rule added"
fi

# Persistence: edit the rules file directly, as Oracle documents for its
# images. Never 'netfilter-persistent save' here: it would also freeze
# Docker's and fail2ban's runtime chains into the file.
if sudo grep -qxF -- "$RULE_LINE" "$RULES_FILE"; then
  ok "rule already in ${RULES_FILE}"
elif sudo grep -q '^-A INPUT -j REJECT' "$RULES_FILE"; then
  sudo sed -i "0,/^-A INPUT -j REJECT/s||${RULE_LINE}\n&|" "$RULES_FILE"
  ok "rule added to ${RULES_FILE}, before the final REJECT"
else
  warn "no '-A INPUT -j REJECT' line in ${RULES_FILE}; add this by hand: ${RULE_LINE}"
fi

# ---------------------------------------------------------------------------
# 4. Publish through the edge
# ---------------------------------------------------------------------------

log "Caddy site for https://${DESKTOP_HOST}"

SITE="${DEV_ROOT}/caddy/sites/_desktop.caddy"
if [[ -f "$SITE" ]]; then
  ok "${SITE} exists (kept, including any edits)"
else
  cat > "$SITE" <<EOF
# Optional web desktop (Guacamole). Generated by devbox-desktop-setup.sh.
${DESKTOP_HOST} {
	# Optional: allow the desktop only from known networks; door B is unaffected.
	# @outside not remote_ip 203.0.113.7/32
	# respond @outside 403
	encode zstd gzip
	reverse_proxy guacamole:8080
}
EOF
  ok "written ${SITE}"
fi

if sudo docker exec -w /etc/caddy "$CADDY_CONTAINER" \
     caddy reload --config /etc/caddy/Caddyfile >/dev/null 2>&1; then
  ok "caddy reloaded"
else
  die "caddy reload failed. Check: sudo docker logs ${CADDY_CONTAINER} --tail 30"
fi

log "Waiting for a valid certificate on https://${DESKTOP_HOST}"
for i in $(seq 1 30); do
  code="$(curl -s -o /dev/null -m 10 -w '%{http_code}' \
           --resolve "${DESKTOP_HOST}:443:127.0.0.1" "https://${DESKTOP_HOST}/" || true)"
  if [[ "$code" =~ ^(200|302)$ ]]; then
    ok "https://${DESKTOP_HOST} -> HTTP ${code}, certificate valid"
    break
  fi
  if [[ $i -eq 30 ]]; then
    warn "no valid HTTPS answer yet (last: ${code:-none}). Check DNS and: sudo docker logs ${CADDY_CONTAINER} --tail 50"
  fi
  sleep 5
done

# ---------------------------------------------------------------------------
# 5. fail2ban for the Guacamole login
# ---------------------------------------------------------------------------

log "fail2ban jail for the Guacamole login"

command -v fail2ban-client >/dev/null 2>&1 || apt_install fail2ban

# Guacamole 1.6 logs 'for user "x" failed: <reason>'; 1.5 logged 'failed.'.
# The address is '[<X-Forwarded-For>, <proxy>]', or a bare client IP once
# Tomcat's RemoteIpValve has consumed the header. The regex accepts all of them.
sudo tee /etc/fail2ban/filter.d/guacamole.conf > /dev/null <<'EOF'
[Definition]
failregex = Authentication attempt from \[?<HOST>(?:, [^\]]*)?\]? for user "[^"]*" failed\b
ignoreregex =
EOF

# Caddy runs in Docker: HTTPS traffic crosses the FORWARD path, never INPUT.
# Ubuntu's default ban action (nftables on the input hook) would never see it,
# so this jail bans with iptables in Docker's DOCKER-USER chain instead.
sudo tee /etc/fail2ban/jail.d/guacamole.conf > /dev/null <<'EOF'
[guacamole]
enabled      = true
backend      = systemd
journalmatch = CONTAINER_NAME=guacamole
filter       = guacamole
banaction    = iptables-multiport
chain        = DOCKER-USER
port         = http,https
maxretry     = 5
findtime     = 10m
bantime      = 1h
EOF

# DOCKER-USER only exists while Docker runs: start fail2ban after it at boot.
sudo mkdir -p /etc/systemd/system/fail2ban.service.d
sudo tee /etc/systemd/system/fail2ban.service.d/after-docker.conf > /dev/null <<'EOF'
[Unit]
After=docker.service
EOF
sudo systemctl daemon-reload
sudo systemctl enable fail2ban >/dev/null
sudo systemctl restart fail2ban
sleep 2
if sudo fail2ban-client status guacamole >/dev/null 2>&1; then
  ok "fail2ban jails: $(sudo fail2ban-client status | grep 'Jail list' | cut -d: -f2)"
else
  warn "guacamole jail not active: journalctl -u fail2ban -n 30"
fi

# ---------------------------------------------------------------------------
# 6. Verification
# ---------------------------------------------------------------------------

log "Verification"

echo "  Listening sockets:"
sudo ss -tlnp | awk 'NR==1 || /:(22|80|443|3389|8080)[[:space:]]/' | sed 's/^/    /'

echo
if sudo iptables -S INPUT | grep -- '--dport 3389' | grep -qv -- "-s ${DOCKER_NET_CIDR}"; then
  warn "an INPUT rule accepts 3389 beyond ${DOCKER_NET_CIDR}. Review: sudo iptables -S INPUT"
else
  ok "3389 accepted only from ${DOCKER_NET_CIDR}; the OCI security list does not open it either"
fi

# ---------------------------------------------------------------------------
# Summary
# ---------------------------------------------------------------------------

cat <<EOF

${GRN}Desktop ready.${RST}

  URL        https://${DESKTOP_HOST}
  User       guacadmin
  Password   guacadmin      ${YLW}<-- enrol TOTP, then change it at first login${RST}

RDP connection to create inside Guacamole:

  Settings -> Connections -> New Connection
     Protocol                       RDP
     Hostname                       host.docker.internal
     Port                           3389
     Username                       ubuntu
     Password                       the one set for 'ubuntu' above
     Security mode                  Any
     Ignore server certificate      yes
     Color depth                    16-bit
     Enable wallpaper               no

If something fails:

  sudo docker compose -f ${GUAC_DIR}/docker-compose.yml logs -f
  sudo docker logs ${CADDY_CONTAINER} --tail 50
  sudo journalctl -u xrdp -f
  sudo iptables -L INPUT -n --line-numbers

EOF
```

---

## Appendix D — Changes from the source documents

Every deviation from `dev-environment-runbook.md`, `oci-remote-desktop-guide.md` and the two original scripts.
The behaviours behind the bug fixes (CLI flag parsing and data directory, Guacamole 1.6 log format, JMESPath
parsing, `awk` regex support, fail2ban's default action) were checked against upstream sources and the actual
tools on 10 September 2026.

| # | Area | Source | This document | Why |
|---|---|---|---|---|
| 1 | Structure | Two documents and two scripts, desktop as the main goal | One procedure; development environment first, desktop optional | Requested scope |
| 2 | Host | "Debian 12 or Ubuntu 22.04+", x86-64 implied | OCI A1, Ubuntu 24.04 aarch64 | The Always Free shape |
| 3 | VS Code CLI | Downloads `cli-alpine-x64` | Architecture detected at build time (`cli-alpine-arm64` on A1) | An x86-64 binary cannot run on ARM64 |
| 4 | Base image | `devcontainers/base:debian-12` | `devcontainers/base:bookworm` | `debian-12` is not among the documented tags (`bookworm`, `debian12`); `bookworm` is published for arm64 |
| 5 | Node.js | 20.x | 22.x LTS | Node 20 reached end of life in April 2026; the Dev Container CLI needs ≥ 20 |
| 6 | Host firewall | `ufw allow 80,443` + `ufw enable` | Nothing opened; ufw left inactive | Oracle warns ufw can leave Ubuntu instances unable to boot; Docker-published ports cross `FORWARD`, not `INPUT` |
| 7 | Desktop TLS | Host Caddy on 443 with internal CA or Let's Encrypt IP certificate, `default_sni` | `desktop.<BASE_DOMAIN>` through `devenv-caddy` | Two Caddy instances cannot share 443; with a domain, ordinary certificates suffice |
| 8 | Desktop location | `~/guacamole`, loopback only | `/srv/dev/desktop`, webapp also on `devenv_edge` | One tree; reachable by the shared edge |
| 9 | Edge network | Created by compose | External, created by the host script and on demand by `dev` | The desktop joins it; `dev down` must not try to delete it |
| 10 | Tunnel flags | `--random-name=false` | Removed | Boolean flag; the CLI (clap 4) rejects a value, so the tunnel would exit and the container restart-loop |
| 11 | Tunnel token | Volume on `~/.vscode-cli` | Plus `VSCODE_CLI_DATA_DIR=/home/vscode/.vscode-cli` | The current CLI uses `~/.vscode/cli` and cannot migrate a mount point; without the variable the token was lost on every rebuild |
| 12 | Tunnel names | Any length | At most 20 characters, enforced by `dev add` | CLI limit |
| 13 | Image pulls | Default | `pull_policy: never` | Never pull `docker.io/devenv/<name>`, a namespace that is not ours |
| 14 | Memory default | `10g` (16 GB host) | `8g` | 12 GB host, with room for the host, the edge and the desktop |
| 15 | CPU rationale | "Leaves half a core for the editor server" | Leaves half a core for the host; the editor shares the quota | Both editor servers run inside the container |
| 16 | Git | Not covered | Devbox key on the host, mounted read-only into containers; identity from `config.env` | Clone and push from the containers |
| 17 | Settings Sync | "Enable in both doors" | Door A only | code-server has no built-in Settings Sync |
| 18 | Placeholders | `<n>` | `<name>` | Restored; columns in `dev`'s usage realigned |
| 19 | Caddy data | `/srv/dev/caddy/data` created, never mounted | Removed from the layout | Certificates live in the `caddy-data` volume |
| 20 | `dev` | — | Config check, network on demand, hostname-collision check, `dev update`, reload warning, backup mode 0600, status with both doors and real limits | Failure modes found while integrating |
| 21 | `dev` limits | "Lower `DEFAULT_CPUS`, `dev build`" | Edit the fragment, `dev up <name>` | Defaults are rendered into the fragment at `dev add`; `dev build` never re-reads them |
| 22 | Heredocs | `<<-` with tab-indented bodies | Plain `<<` | Survives tabs turning into spaces when copy-pasted |
| 23 | Security list | `OPEN_HTTPS=false`, 443 only; check via JMESPath | `OPEN_WEB=true`, 80 and 443; idempotent merge in Python | Caddy needs both ports; the original query was a JMESPath syntax error, so the rule was re-added on every run |
| 24 | Region | Fixed `eu-frankfurt-1` | Checked against the tenancy home region | Always Free compute exists only there |
| 25 | Instance lookup | `RUNNING` only | Any non-terminated state; stopped instances are started | A re-run must never launch a twin |
| 26 | Tenancy | Derived from the compartment list | `$OCI_TENANCY` first (Cloud Shell), then the list | Works in tenancies without compartments |
| 27 | fail2ban filter | `... for user "x" failed\.` with a bracketed address | Accepts `failed: <reason>` and a bare address | On Guacamole 1.6.0 with RemoteIpValve the original regex matched nothing |
| 28 | fail2ban action | Default action on `INPUT` | `iptables-multiport` in `DOCKER-USER`, fail2ban ordered after Docker | Docker-published traffic never crosses `INPUT`; Ubuntu 24.04's default action is nftables on the input hook |
| 29 | iptables persistence | `netfilter-persistent save` | Rule inserted into `rules.v4` before the final `REJECT` | A save freezes Docker's and fail2ban's runtime chains into the file |
| 30 | 3389 check | `nc` to the instance's own public IP | Inspect the `INPUT` rules | A hairpin connection proves nothing about the host firewall |
| 31 | Socket listing | `awk '/...\s/'` | `[[:space:]]` | Ubuntu's `mawk` does not support `\s`; only the header was printed |
| 32 | Language | Script comments and messages in Spanish | English | Documentation language |
| 33 | New context | — | Idle reclamation, Cloud Shell limits, reserved IP before DNS, CAA, ARM64 toolchains | Needed to go from nothing to a working environment |
