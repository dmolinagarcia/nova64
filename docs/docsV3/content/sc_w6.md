# Creating and configuring the development environment
> Complelety browser based. Self-contained setup scripts

This document will guide you through all the needed steps to setup a web-based development environment where you will be able to create your own noVA64-emu, and, as the emulator itself is web based, you will be able to host it too. The whole premise of this document is for the development environment to be 100% free.

!! WARNING! As with everything cloud-based, there are some key steps where, if not executed properly, you may incur into cost. 
This steps will be clearly pointed out, but, please be advised, I cannot take responsability for any expenses you may generate. Mistakes happen, cloud providers contracts change over time, and what is valid today, may not me valid tomorrow.

The scripts to setup the environment are self-contained in this document: every script, template and configuration file is reproduced here in full and can be extracted automatically (section 4). 

- **Host:** OCI Always Free `VM.Standard.A1.Flex`, 2 OCPU / 12 GB, Ubuntu 24.04 aarch64, tenancy home region
- **Access model:** browser-only, through two independent entry points.
- **Door A** — `code tunnel` → `https://vscode.dev/tunnel/<name>` (outbound 443 only)
- **Door B** — `code-server` behind Caddy → `https://<name>.<BASE_DOMAIN>` (inbound 443)
- **Environment model:** one container per project, defined by `devcontainer.json`
- **Management:** a single `dev` command wrapping the whole lifecycle
- **Administration:** OCI Cloud Shell, so even the setup needs nothing but a browser
- **Optional:** an XFCE desktop in the browser (Guacamole), published on the same edge — section 12

**Door B** requires a domain you can manage, and it is provided in case your network blocks access to *vscode.dev*

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
- Appendix D — devbox-oci-teardown.sh

---

## 1. What this builds

Access paths:

```
door A   browser ── HTTPS 443 ──► vscode.dev relay ◄── outbound 443 ── code tunnel ────┐
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

Both doors serve the same container, the same files and the same processes. They differ only in transport and in which extension marketplace is reachable: door A reaches the official Marketplace (Copilot and Microsoft-proprietary extensions work), door B reaches Open VSX only.

Running both is deliberate. The failure this environment exists to solve is a restrictive network, and the two doors fail independently: proxies with TLS inspection sometimes block `vscode.dev` or break its WebSocket upgrade, while a plain HTTPS request to your own domain still passes.

Inbound, the instance accepts 22 (SSH, key only), 80 (ACME challenge and redirects) and 443. Everything else is rejected by the image's host firewall, and the OCI security list opens nothing more. code-server's port 8080 is never published, and the desktop's xrdp and Guacamole ports are not reachable from outside.

The build is three scripts plus one management command, all contained in this document:

| Piece | Runs on | Does |
|---|---|---|
| `devbox-oci-setup.sh` (Appendix A) | Cloud Shell | compartment, network, security list, SSH key, instance, reserved IP |
| `devbox-host-setup.sh` (Appendix B) | the instance | Docker, Dev Container CLI, swap, `/srv/dev`, Git key, fail2ban |
| `dev` (section 18.8) | the instance | project lifecycle: add, build, update, login, status, backup |
| `devbox-desktop-setup.sh` (Appendix C) | the instance, optional | XFCE, xrdp and Guacamole behind the same edge |
| `devbox-oci-teardown.sh` (Appendix D) | Cloud Shell | deletes everything Appendix A created, in reverse order |

All of them are idempotent and safe to re-run.

Although this build is aimed at noVa64-emu development, it is possible to start new containers within your OCI instance to host other project but, keep in mind, resources in the server are limited and no more than one container can run at the same time.

!! You can choose to create an instance with more resources, but anything above the allocations defined in this document will exceed OCI Always-Free limits, and incur in costs.

---

## 2. Why the editor runs inside the container

The Dev Containers extension cannot be installed when connecting to a remote host from `vscode.dev` in a browser. It works from desktop VS Code against the same host, but not from the web client. The "browser → host → container" path that GitHub Codespaces appears to offer is therefore not reproducible from a browser.

The design consequence is structural: **the editor server runs inside the project container**, and the browser connects directly to it. This is what Codespaces does internally. `devcontainer.json` is still the source of truth for the image, but it is used at *build* time only — never at connect time.

---

## 3. Constraints worth knowing before you start

**This document won't guide you through creating your Oracle OCI account.** As cloud providers change their interfaces too often, these instructions could become obsolete or incorrect too soon. Goto [cloud.oracle.com](https://cloud.oracle.com) and follow the instructions there. Even though we will be using the Always-Free tier, you still need a valid credit card to sign up.

**The Ampere A1 free allowance was halved.** Oracle reduced it from 4 OCPUs / 24 GB to 2 OCPUs / 12 GB (1,500 OCPU-hours and 9,000 GB-hours per month) effective 15 June 2026. Free-tier accounts were emailed that instances above the new limit would be disabled from 18 August 2026. Reports on whether Pay As You Go accounts kept the old allocation contradict each other. Size the instance
at 2 / 12; the provisioning script checks the real quota before launching.

**Always Free resources only exist in your home region.** That region is selected during signup and cannot be changed. The provisioning script defaults to `eu-frankfurt-1` and will fail to execute against any region other than the tenancy's home region. Frankfurt has three availability domains, which helps with capacity; most regions have only one.

**A1 capacity is frequently exhausted.** `Out of host capacity` on instance launch is normal, not a misconfiguration. The provisioning script rotates through all availability domains and retries. Cloud Shell sessions time out after 20 minutes without keyboard activity and last 24 hours at most: for a long capacity fight, keep the session active or run the script from a machine that stays on.

**Idle instances can be reclaimed on Free Tier accounts.** Oracle's Always Free documentation lets it reclaim Always Free compute instances that stay idle — low CPU, network and memory utilisation — over a 7-day window. A development box idles most of the week. Oracle's own notice states that converting the account to Pay As You Go prevents this, and Always Free resources stay free after the upgrade. Recommended, together with the budget alert in section 5.

!! WARNING! Switching to a PAYG account will keep your instances running but, if you exceed the Free-Tier service limits, you will be charged.

**The host is ARM64.** Everything in this runbook is architecture-aware, and the open-source toolchains in Debian's archive (GCC, Yosys / nextpnr / IceStorm, cc65, KiCad and many more) are built for arm64. Vendor tools shipped only as x86-64 binaries do not run natively.

**Do not use ufw (Uncomplicated FireWall).** Oracle's Ubuntu images ship iptables rules in `/etc/iptables/rules.v4` that allow SSH, reject everything else, and keep the instance's volumes reachable. Oracle warns that enabling ufw can leave the instance unable to boot. This runbook never touches ufw: Docker publishes Caddy's ports through the `FORWARD` chain it manages, and the single host rule the optional desktop needs is added to `rules.v4` directly.

**Door B needs a domain you own.** Each project gets its own hostname under a wildcard DNS record. Free dynamic DNS providers are a poor fit: FreeDNS (afraid.org) shared domains are not on the Public Suffix List — inclusion requests must come from the domain registrant, and the proposals never progressed — and Let's Encrypt counts issuance limits per registered domain, so every user of a shared domain draws from the same quota and `too many certificates already issued` is common. Without a domain, door A still works on its own.

!! WARNING! Buying your own domain costs money. Not a lot, but money anyhow. 

**Bare-IP HTTPS is not used.** The TLS specification does not permit address literals in the Server Name Indication extension, so a browser opening `https://<ip>` sends no SNI at all; Caddy selects certificates by SNI, and the handshake fails with `ERR_SSL_PROTOCOL_ERROR`. The source desktop guide worked around this with `default_sni` plus either Caddy's internal CA or a short-lived Let's Encrypt IP certificate (generally available since 15 January 2026, about 160 hours of validity). With a domain none of that is needed: every hostname, the desktop included, gets an ordinary certificate automatically.

---

## 4. How to use this document

Everyfile that you need to run is contained in this runbook. Large scripts have some hidden marker before the actual contents that instruct the extractor to create that file. Files will be created within your Cloud Shell envinronment under `~/devbox-kit/<path>` with proper permissions set. This procedure avoids the need to copy logn scripts, which could potentially introduce some hard to debug errors.

Smaller scripts can (and will) be copy/pasted from this document manually.

1. Open Cloud Shell (OCI Console → Developer tools → Cloud Shell) and fetch this sheet, which is the file the extractor reads:

~~~bash
curl -fsSL -o devbox-oci-runbook.md \
  https://raw.githubusercontent.com/dmolinagarcia/nova64/main/docs/docsV3/content/sc_w6.md
~~~

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

3. You should get twelve files:

```
devbox-kit/
├── devbox-oci-setup.sh            Appendix A — run in Cloud Shell
├── devbox-host-setup.sh           Appendix B — run on the instance
├── devbox-desktop-setup.sh        Appendix C — optional, run on the instance
├── devbox-oci-teardown.sh         Appendix D — run in Cloud Shell to destroy it all
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

Re-running the extractor overwrites the kit, never anything under `/srv/dev`.

---

## 5. Phase 0 — Prerequisites

1. **An OCI account.** Note its home region; the Console shows it in the region selector and on the tenancy details page.
2. **A decision on Pay As You Go.** Recommended because of the idle-reclamation policy (section 3): Billing & Cost Management → Upgrade and Manage Payment. Oracle does not charge for Always Free resources after the upgrade, only for usage above the Always Free limits.
3. **A budget alert**, whatever the account type: Billing & Cost Management → Budgets → Create Budget, target the root compartment, amount 1 EUR, alert rule at 1 % of actual spend, your email. Oracle has changed the A1 allowance without notice more than once; this gets you an email within hours rather than a surprise at month end.
4. **A workstation.** OCI Cloud Shell is recommended: it runs in the browser, has the OCI CLI already authenticated, keeps a persistent home directory and can open outbound SSH. Any Linux or macOS machine with the OCI CLI (`oci setup config`) and `ssh-keygen` also works.
5. **A domain you control**, with DNS you can edit. Section 7 creates one wildcard record. If the domain publishes CAA records, they must allow `letsencrypt.org`.
6. **A GitHub account.** Door A signs in with it, and the devbox's Git key is added to it in section 8.

The scripts fix these names: compartment `devbox`, VCN `vcn-devbox`, instance `devbox`. Project names become tunnel names and hostnames: lowercase letters, digits and hyphens, at most 20 characters.

---

## 6. Phase 1 — Provision the OCI infrastructure

Make sure you have extracted the scripts as explained in section 4. Then, from Cloud Shell:

```bash
bash ~/devbox-kit/devbox-oci-setup.sh
```

It creates, in this order: the `devbox` compartment, VCN `vcn-devbox` (10.0.0.0/16), an internet gateway, a default route, a public subnet (10.0.0.0/24), ingress rules for TCP 80 and 443 on the default security list, an SSH keypair (`~/.ssh/oci_devbox`), the `devbox` instance, and the reserved public IP `devbox-public-ip` assigned to it.

Notable behaviours:

- Every resource is looked up by name before creation, so re-running after a failed launch reuses what exists instead of duplicating it. An existing instance in any state other than terminated is reused — a stopped one is started — so a re-run never launches a second instance.
- It refuses to run against a region other than the tenancy's home region. If yours is not Frankfurt: `REGION=<home-region> bash ~/devbox-kit/devbox-oci-setup.sh`.
- On `OutOfHostCapacity` it rotates through all availability domains and retries every 60 seconds. On any other error it stops and prints the message, rather than hammering the API indefinitely.
- Ingress for 80 and 443 is added only if missing, and it is safe to add before anything listens: the image's host firewall rejects everything except SSH until Docker publishes Caddy. Port 80 stays open to `0.0.0.0/0` because ACME validation comes from many addresses; 443 as well, because door B must be reachable from every network you work from. `OPEN_WEB=false` leaves the security list alone.
- The boot volume is 100 GB with default VPU. Size is free within the 200 GB block storage allowance; raising the performance tier is not.
- **The instance is launched with no public IP at all**, and a reserved one is assigned to its primary private IP immediately afterwards (section 7 explains why). The reserved IP is looked up by name like everything else, so a re-run reuses it: it is created on the first run, re-assigned if it was left unassigned — after a teardown, or after the instance was rebuilt — and left alone when it is already in place.
- A private IP holds at most one public IP. An instance created by an earlier version of this script already has an ephemeral address, so the script releases it before assigning the reserved one, and warns you that the address changes at that moment.
- Resulting OCIDs and the public IP are written to `~/devbox.env`.

When it finishes:

```bash
source ~/devbox.env
ssh -i ~/.ssh/oci_devbox ubuntu@$IP      # or simply: devbox
```

**Back up the SSH private key.** Download `~/.ssh/oci_devbox` (Cloud Shell's menu has a Download option) and keep it somewhere safe. It is the only credential that opens SSH on the instance, and Oracle removes Cloud Shell home directories after a long period without use. Losing this file means losing access to your server!

!! WARNING! I have just said it, but it is not enough. Download your private key. Keep it somewhere safe. If you lose it you will potentially lose your server. Keep it away from other people, they could impersonate you and access your server.

To manage the ingress rules by hand instead: Networking → Virtual Cloud Networks → `vcn-devbox` → Security Lists → Default → Add Ingress Rules.

| Protocol | Port | Source | Purpose |
|---|---|---|---|
| TCP | 80 | 0.0.0.0/0 | ACME HTTP challenge, HTTP → HTTPS redirects |
| TCP | 443 | 0.0.0.0/0 | door B and the optional desktop |

Your workstation's egress address and the instance's public address are different things. Get the former with `curl -s ifconfig.me` from the workstation, and the latter from the metadata service on the instance:

```bash
curl -s -H 'Authorization: Bearer Oracle' http://169.254.169.254/opc/v2/vnics/ | grep publicIp
```

---

## 7. Phase 2 — Stable public IP and DNS

The address is already reserved: section 6 launched the instance with no public IP and assigned it a reserved one called `devbox-public-ip`. This matters because an ephemeral address lives and dies with the instance, while a reserved one survives termination and can be attached to a replacement — so the DNS record you are about to create keeps working when you rebuild the instance, and there is no manual swap to perform in the Console.

Only DNS is left.

1. **Create the DNS record.** Choose the base domain for door B — this runbook uses `dev.example.com` — and create a wildcard A record pointing at the address the script printed:

   ```bash
   source ~/devbox.env && echo "$IP"
   ```

   ```
   *.dev.example.com.   300   IN   A   <reserved IP>
   ```

   Projects become `<project>.dev.example.com` and the optional desktop `desktop.dev.example.com`. A wildcard means `dev add` never needs a DNS step.

2. **Verify before continuing.** Caddy requests certificates as soon as a site appears, and repeated failures count against Let's Encrypt's limits:

   ```bash
   dig +short anything.dev.example.com      # must print the reserved IP
   ```

---

## 8. Phase 3 — Host setup

Copy the kit to the instance and run the host script as `ubuntu`, not as root:

```bash
source ./devbox.env
scp -i ~/.ssh/oci_devbox -r ~/devbox-kit ubuntu@$IP:~/
ssh -i ~/.ssh/oci_devbox ubuntu@$IP
```

Before running the deploy setup script, we need to amend sudo permisions. As ubuntu

```bash
sudo visudo -f /etc/sudoers.d/99-nopasswd-ubuntu
```

And add this content
```
ubuntu ALL=(ALL:ALL) NOPASSWD: ALL
Defaults:ubuntu !authenticate
```

Exit your server session and login again. Then, run the setup script

```bash
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

**Why the host firewall is left alone.** Oracle's Ubuntu images load `/etc/iptables/rules.v4`: SSH allowed, then a final `REJECT` on both `INPUT` and `FORWARD`. Traffic to a port that Docker publishes is DNAT-ed in `PREROUTING` and crosses `FORWARD`, where Docker inserts its own accept rules ahead of that `REJECT`; it never touches `INPUT`. Caddy's 80 and 443 therefore work without any host rule, and `INPUT` keeps protecting everything else. Two things to avoid on this host: ufw (section 3), and `iptables-restore` or `netfilter-persistent reload`/`save` while Docker runs. A restore flushes Docker's chains and breaks container networking until Docker restarts; a save freezes Docker's and fail2ban's runtime rules into the file.

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

Only `projects/` holds real work, and it is bind-mounted rather than copied so that an image rebuild never touches source. Git remotes remain the actual backup; the bind mount is convenience, not durability. ACME certificates live in the `caddy-data` Docker volume.

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

Caddy starts with no sites configured, which is expected; it obtains certificates lazily as sites appear. `dev up` refuses to start while `BASE_DOMAIN` or `ACME_EMAIL` still hold placeholders: an empty email makes the Caddyfile unparseable, and Caddy would restart-loop.

### Hardening door B

**What you already have.** Every project gets its own long random password, with no action on your part:
`dev add` generates one with `openssl rand -base64 24`, writes it to `/srv/dev/secrets/<project>.password`
(mode 0644, inside the 0700 `secrets/` directory), and the compose fragment mounts it into the container as the Docker secret
`code_server_password`. The entrypoint reads it and starts code-server with `--auth password`. The password is
printed when the project is created; `cat /srv/dev/secrets/<project>.password` shows it again later, and
writing a new value into that file followed by `dev restart <project>` rotates it.

That file is the whole of door B's authentication. If it is missing when the container starts, the entrypoint
prints `WARNING: no password secret found` and starts code-server with `--auth none` — an editor with a
terminal, published on your domain, with no login at all. It is worth grepping for after any manual surgery on
`/srv/dev/secrets`:

```bash
dev logs <project> | grep -i "no password secret"
```

**What to add**, in descending order of value. Do at least the first before relying on door B from untrusted
networks.

1. **An identity layer in front**, so the code-server login page is never the outermost defence. Cloudflare Access is the least work; Authelia if you want it self-hosted; Caddy `basic_auth` as an absolute minimum.

   No project exists yet at this point in the runbook, and per-project site files are generated by `dev add`
   from `templates/site.caddy.tpl` (section 18.6). So edit the **template**: every project created from now on
   is protected at birth, and there is nothing to remember later.

   ```bash
   HASH="$(docker exec devenv-caddy caddy hash-password --plaintext 'your-password')"
   echo "${#HASH}"        # must print 60; anything else means the hash is truncated
   ```

   ```bash
   cat > /srv/dev/templates/site.caddy.tpl <<EOF
   __NAME__.__BASE_DOMAIN__ {
   	encode zstd gzip
   	basic_auth {
   		your-username ${HASH}
   	}
   	reverse_proxy devenv-__NAME__:8080
   }
   EOF
   ```

   The heredoc is deliberately **unquoted**, so `${HASH}` expands; the `$2a$14$` inside the hash is not
   re-expanded, because the result of an expansion is not rescanned. Never paste a bcrypt hash by hand out of a
   terminal — a wrapped line silently loses characters, Caddy loads the truncated hash without complaint, and
   every login then fails with a 401 that looks exactly like a wrong password (section 16).

   All projects share these credentials, which is the point: this layer identifies *you*, while the per-project
   password identifies the project. The browser asks for these first, and code-server asks for its own
   afterwards.

   To protect a project that already exists, put the same `basic_auth` block into its
   `/srv/dev/caddy/sites/<project>.caddy` and run `dev reload`. Do not create that file for a project you have
   not added yet: `dev add` refuses to run when the hostname is already claimed by a site file, because two
   blocks for one hostname make Caddy reject the whole configuration.

2. **crowdsec or fail2ban reading the Caddy access log.** Enable Caddy's `log` directive first; it records no requests by default. With fail2ban, ban in Docker's `DOCKER-USER` chain as section 12 does, because this traffic never crosses `INPUT`.
3. **No inbound ports at all.** If you can accept it operationally, replace Caddy with a Cloudflare Tunnel (`cloudflared`) so door B is also outbound-only. Same container, same code-server, nothing published on the VM.

Never expose code-server on a published host port without TLS and authentication in front of it.

---

## 10. Phase 5 — First project and tunnel login
We will now create the project container for noVa64.

```bash
dev add nova64 git@github.com:dmolinagarcia/nova64.git
```

This clones the repository, scaffolds `.devcontainer/` if the repo has none, generates the password, the
compose fragment and the Caddy site, builds the image through the Dev Container CLI so that `features` are
honoured, starts the container and reloads Caddy. `dev add nova64` without a URL starts an empty project.

If the repository already carries a `devcontainer.json`, it is used unchanged — but the image must contain the
two editor servers and the pre-created home directories from `Dockerfile.tpl` (section 18.7), or neither door
will start. Add those blocks to the project's own Dockerfile, keeping them architecture-aware as written: the
devbox is ARM64, GitHub Codespaces is x86-64, and the same file has to build on both.

A repository that names a ready-made image with `"image":` rather than building one almost certainly needs
converting to `"build": {"dockerfile": "Dockerfile"}`, for two independent reasons: the image has no reason to
carry code-server or the VS Code CLI, and it may not be published for arm64 at all. Microsoft's
`devcontainers/universal`, the Codespaces default, is amd64-only and fails the build outright with
`no matching manifest for linux/arm64/v8`. `devcontainers/base:bookworm` — what `Dockerfile.tpl` builds on —
is multi-arch. Check before assuming:

```bash
docker manifest inspect <image> | grep architecture | sort -u
```

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
project**, not on every rebuild.

Tunnel names are unique per GitHub account, and a registration belongs to the account, not to the machine —
deleting the container, the volume or the whole instance does not release the name. There are two ways to
release one, and only the second survives losing the host:

```bash
docker exec devenv-<project> code tunnel unregister     # from the container that holds it
```

`code tunnel unregister` takes no arguments: it removes the association of *the machine it runs on*, reading
the registration from that machine's CLI data directory. Hence `docker exec` into the right container — there
is no way to name a different tunnel from the command line.

The second way works from anywhere and needs nothing of the original machine: open any VS Code client —
desktop or `vscode.dev` — signed in with the same GitHub account, go to the **Remote Explorer** view, find the
machine under Remote Tunnels, right-click it and choose **Unregister**. That same view is the only account-wide
list there is; the CLI has no command that enumerates tunnels.

To see which name a container holds, and whether it still holds one:

```bash
docker exec devenv-<project> code tunnel status
# {"tunnel":null,"service_installed":false}   -> this container has no tunnel
```

Opening `https://vscode.dev/tunnel/<name>` also tells you whether a specific name is taken. Section 13 has a
one-liner that reports the names held by every container on this host.

Two service-side behaviours worth knowing, both from Microsoft's dev tunnels documentation: **an account holds
at most 10 registered tunnels** — past that, creating one makes the CLI "pick a random unused tunnel and delete
it" — and **an unused tunnel is deleted after 30 days of inactivity** by default. An orphaned name therefore
frees itself eventually, but not on any schedule you would want to wait for.

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

### Which tunnel names are registered

The CLI has no command that lists the tunnels on your account — `code tunnel` offers `status`, `rename` and
`unregister`, all of them about the machine they run on. Every tunnel in this environment is registered from a
project container, so asking each container is the complete answer for this host:

```bash
for c in $(docker ps -a --format '{{.Names}}' | grep '^devenv-' | grep -v '^devenv-caddy$'); do
	printf '%-24s %s\n' "$c" "$(docker exec "$c" code tunnel status 2>/dev/null || echo 'not running')"
done
```

`{"tunnel":null,...}` means that container holds no name. A container that is not running cannot be asked;
start it, or read the registration straight off its volume:

```bash
docker run --rm -v devenv_<project>-cli:/d alpine cat /d/code_tunnel.json
```

For names left behind by containers that no longer exist — a rebuilt or torn-down devbox — the view is VS
Code's Remote Explorer under Remote Tunnels, signed in with the same GitHub account, on the desktop app or
`vscode.dev`. Right-click → **Unregister** releases a name from there, which is the only route once the host
is gone. `dev remove` does it the tidy way, from inside the container, before deleting anything.

From a browser-only location, administration goes through Cloud Shell: `source ~/devbox.env && devbox`.

`DEFAULT_CPUS` and `DEFAULT_MEM` apply when a project is created. To change one existing project, edit
`cpus:` / `mem_limit:` in `/srv/dev/projects/<name>/devenv.compose.yml` and run `dev up <name>`; compose
recreates the container with the new limits.

---

## 14. Vendor and proprietary toolchains

Anything that cannot be installed from a package repository — licensed compilers, vendor FPGA suites,
proprietary SDKs — should not be baked into a public image or committed to the repository. Two workable
patterns:

1. **Mount the installer.** Add a read-only bind mount of a host directory holding the installer and licence file to the project's compose fragment, and run the installer once from a shell inside the container (`dev shell <project>`), or from a `RUN` line in the Dockerfile that consumes the mount at build time. **Not from `postCreateCommand`:** lifecycle hooks never run here (see below), so an installer wired to one silently never executes.
2. **Private base image.** Build a private image once with the vendor tool inside, push it to a private registry, and have `devcontainer.json` use it as the `FROM`.

**`devcontainer.json` is a build manifest here, not a lifecycle manifest.** `dev build` calls `devcontainer
build`, which only produces an image; the container is then started by docker compose with the entrypoint from
section 18.4. Everything the Dev Containers CLI would normally run at *connect* time is therefore ignored:

| Ignored on the devbox | Where the equivalent lives |
|---|---|
| `postCreateCommand`, `postStartCommand`, `onCreateCommand`, `postAttachCommand` | a `RUN` line in the Dockerfile, or a command you run in `dev shell <project>` |
| `forwardPorts`, `portsAttributes` | the editor's own port forwarding, once connected through either door |
| `runArgs`, `mounts`, `containerEnv` | the project's `devenv.compose.yml` (section 18.5) |
| `remoteUser` | honoured — it is baked into the image at build time |

`features` and everything else that shapes the image work normally, which is the reason the Dev Containers CLI
is used as the builder at all. The same `devcontainer.json` keeps working unchanged on GitHub Codespaces,
where the ignored keys do take effect.

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

### Destroying the environment

`devbox-oci-teardown.sh` (Appendix D) deletes what `devbox-oci-setup.sh` created, in reverse order: instance,
boot volumes, reserved IP, subnet, route rules, internet gateway, the 80/443 ingress rules, VCN. It finds each resource by
the same name the setup script looks up, so it removes what that script would have reused and leaves anything
else alone — an object it did not create is reported at the end, never deleted.

```bash
DRY_RUN=true bash ~/devbox-kit/devbox-oci-teardown.sh    # inventory only, deletes nothing
bash ~/devbox-kit/devbox-oci-teardown.sh                 # asks you to type the compartment name
```

Always look at the dry run first. **The boot volume goes with the instance**, and everything under `/srv/dev`
with it: projects, secrets, the Git key, the ACME certificates. Push your work first, and `dev backup` and copy
the tarball off the instance if you want the environment itself back.

The reserved IP goes with everything else: the setup script creates it, so the teardown removes it, and the
address is gone for good — a new one will not be the same. Two things are kept by default, because losing them
costs more than leaving them, and one is worth keeping deliberately:

| Default | Why | Change it with |
|---|---|---|
| the reserved IP is **deleted** | it is part of what Appendix A creates | `KEEP_RESERVED_IP=true` — the wildcard DNS record keeps resolving, and the next `devbox-oci-setup.sh` re-assigns that same address to the new instance |
| the `devbox` compartment is kept | empty and free; re-running Appendix A reuses it | `DELETE_COMPARTMENT=true` |
| `~/.ssh/oci_devbox`, `~/devbox.env` are kept | local files, not OCI resources | `DELETE_LOCAL=true` |

`KEEP_RESERVED_IP=true` is the right choice when you are rebuilding rather than leaving: the address survives,
so DNS needs no edit and no certificate has to be re-issued.

Compartment deletion is asynchronous, takes minutes, and the compartment stays visible in `DELETED` state
afterwards. It also fails while anything is still inside it — keeping the reserved IP is enough to block it, and
the script warns you about that combination before it starts.

Outside OCI, and outside the script's reach: the wildcard DNS record, the devbox key on your GitHub account,
and the tunnel names, which belong to the GitHub account and survive the instance. Releasing them **before**
the teardown is tidier — `dev remove` does it, and `code tunnel unregister` needs the container that holds the
name — but nothing is lost if you forget: Remote Explorer → right-click the machine → Unregister works
afterwards, from any VS Code client (section 10). Names left registered are what makes a rebuild reject the
same project name (section 16).

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
| `dev add` / `dev build`: `no matching manifest for linux/arm64/v8` | The `devcontainer.json` names an amd64-only image, e.g. `devcontainers/universal` | Convert it to `"build": {"dockerfile": ...}` on a multi-arch base (section 10); `docker manifest inspect <image> \| grep architecture` |
| `postCreateCommand` (or any lifecycle hook) never runs | Expected: the image is built by the Dev Containers CLI, but the container is started by compose | Move the work into the Dockerfile or `dev shell` (section 14) |
| Tunnel name rejected | Name registered to the account (10 per account, kept even if the machine is gone), or longer than 20 characters | `docker exec devenv-<project> code tunnel unregister`, or Remote Explorer → right-click → Unregister (section 10); `dev add` enforces the length |
| `dev up` or `dev add` stops with "set BASE_DOMAIN" / "set ACME_EMAIL" | Placeholders still in `config.env` | Edit `/srv/dev/config.env` |
| Caddy restart-loops | Invalid Caddyfile or site file: empty email, duplicate hostname, typo | `docker logs devenv-caddy`; fix the file; `dev up` |
| Caddy cannot obtain a certificate | DNS not pointing at the IP, 80/443 missing from the security list, or a CAA record | `dig +short <host>`; section 6 ingress table; `docker logs devenv-caddy` |
| `ERR_SSL_PROTOCOL_ERROR` on `https://<ip>` | Expected: no certificate for a bare IP, and browsers send no SNI for IP literals | Use the hostname |
| `basic_auth` asks for the password again and again | Truncated bcrypt hash: Caddy loads it happily and every login 401s. A malformed one would stop Caddy from starting, so a silent 401 means a plausible-but-wrong hash | `awk '$2 ~ /^\$2[aby]\$/ {print $1, length($2)}' /srv/dev/caddy/sites/<project>.caddy` must print 60; regenerate with the `HASH=` recipe in section 9. Test with `curl -u user:pass` — browsers resend cached bad credentials |
| `dev add` stops: "hostname ... is already served by another site file" | A site file for that hostname exists, usually hand-written before the project was created | `rm /srv/dev/caddy/sites/<project>.caddy`, then `dev add`; put shared hardening in `templates/site.caddy.tpl` instead (section 9) |
| `502` from door B | code-server not running, or container not on `devenv_edge` | `dev logs <name>`; `dev restart <name>` |
| `dev add` failed during the build | Toolchain or Dockerfile error | Fix the Dockerfile, `dev build <name>`, then `dev reload` (the site was written before the build) |
| Whole VM unresponsive during builds | Container limit too high | `dev status`; lower `cpus:` in the project's fragment, `dev up <name>` |
| Only the editor lags during builds | The editor shares the container's quota with the build | `nice -n 10`, fewer parallel jobs |
| Extension installs on door A but not B | Not published on Open VSX | Side-load the `.vsix`, or do that work on door A |
| Workspace not writable, `git status`: `error reading .git`, push fails reading the key | Image built before `dev build` remapped `vscode` to the host UID (Oracle's Ubuntu makes `ubuntu` 1001; the image's `vscode` is 1000) | `docker exec devenv-<name> id -u` must equal `id -u` on the host. If not: `dev build <name>`; if the named volumes were already written by the old UID, `docker rm -f devenv-<name>`, `docker volume rm devenv_<name>-cli devenv_<name>-server devenv_<name>-codeserver`, then `dev build <name>` (costs one `dev login`) |
| Container log: `cat: /run/secrets/code_server_password: Permission denied` | Secret file is 0600 and the host user is not UID 1000; compose mounts it with the host owner and ignores `uid`/`mode` for file secrets | `chmod 644 /srv/dev/secrets/<project>.password && dev restart <project>` — `secrets/` is 0700, so the host side stays private |
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
	# 0644 on purpose. Compose bind-mounts file secrets with their host owner and
	# mode, and ignores uid/gid/mode for them, so a 0600 file is unreadable to
	# the container's vscode (UID 1000) whenever the host user is not UID 1000.
	# The host side stays protected by secrets/ itself, which is mode 0700.
	openssl rand -base64 24 > "${DEV_ROOT}/secrets/${name}.password"
	chmod 644 "${DEV_ROOT}/secrets/${name}.password"

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

# The workspace, the Git key and the password secret are bind-mounted with
# their host owner, and the image's vscode is UID 1000. Where the user running
# dev is not UID 1000 -- Oracle's Ubuntu image makes ubuntu 1001 -- nothing in
# the workspace is writable, git cannot read .git and the key is unreadable.
# The Dev Containers CLI fixes this with updateRemoteUserUID, but only on
# `devcontainer up`, which this setup never runs; so remap as a last layer.
match_host_uid() {
	local image="devenv/$1:latest" uid gid
	uid="$(id -u)"; gid="$(id -g)"
	if [[ "$(docker run --rm --entrypoint id "$image" -u vscode)" == "$uid" \
		&& "$(docker run --rm --entrypoint id "$image" -g vscode)" == "$gid" ]]; then
		return 0
	fi
	info "remapping vscode in the image to host UID ${uid}:${gid}"
	docker build -q -t "$image" --build-arg BASE="$image" \
		--build-arg HOST_UID="$uid" --build-arg HOST_GID="$gid" - >/dev/null <<'EOF'
ARG BASE
FROM ${BASE}
ARG HOST_UID
ARG HOST_GID
USER root
RUN groupmod -o -g "$HOST_GID" vscode \
 && usermod -o -u "$HOST_UID" -g "$HOST_GID" vscode \
 && chown -R vscode:vscode /home/vscode
USER vscode
EOF
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
	match_host_uid "$name"
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
	# Release the tunnel name FIRST. 'code tunnel unregister' takes no name: it
	# unregisters the machine it runs on, reading the registration from
	# VSCODE_CLI_DATA_DIR -- which lives in the -cli volume deleted just below.
	# Once that volume is gone the name stays registered to the GitHub account,
	# counting against its limit of 10, until it is unregistered from the Remote
	# Explorer of any VS Code client or expires after 30 days of inactivity.
	if docker exec "devenv-${name}" code tunnel unregister >/dev/null 2>&1; then
		info "tunnel name '${name}' released"
	else
		info "could not release the tunnel name here: the container is not running"
		info "  from this host:  docker start devenv-${name} && docker exec devenv-${name} code tunnel unregister"
		info "  or afterwards:   VS Code -> Remote Explorer -> right-click '${name}' -> Unregister"
	fi
	compose rm -sf "devenv-${name}" || true
	docker volume rm "devenv_${name}-cli" "devenv_${name}-server" "devenv_${name}-codeserver" 2>/dev/null || true
	rm -f "${DEV_ROOT}/caddy/sites/${name}.caddy" \
		"${DEV_ROOT}/projects/${name}/devenv.compose.yml" \
		"${DEV_ROOT}/secrets/${name}.password"
	cmd_reload
	info "removed '${name}'; source left at ${DEV_ROOT}/projects/${name}"
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
#   -> reserved public IP, assigned to the instance's primary private IP
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

# The instance is launched with no public IP and gets this reserved one instead.
# A reserved IP survives termination, so the DNS record outlives the instance.
RESERVED_IP_NAME="devbox-public-ip"

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
  ssh-keygen -b 2048 -t rsa -C "devbox-oci" -f "$SSH_KEY" -N "" >/dev/null  
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
      --assign-public-ip false \
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
# 9. Reserved public IP
# ---------------------------------------------------------------------------
#
# The instance is launched without a public IP, and the reserved one is
# assigned here. An ephemeral address is created and destroyed with the
# instance; a reserved one survives it, so the wildcard DNS record from
# section 7 keeps pointing at the right place when the instance is rebuilt.
#
# A private IP holds at most one public IP, so an instance that already has an
# ephemeral address — anything created by a version of this script older than
# the reserved-IP support — must give it up first. That is the same swap the
# Console does under IP Administration, and the address changes when it happens.

log "Primary VNIC"
VNIC=""
for _ in $(seq 1 20); do
  VNIC="$(ociq oci compute instance list-vnics --instance-id "$INST" \
           --query 'data[0].id' --raw-output)"
  [[ -n "$VNIC" ]] && break
  sleep 5
done
[[ -n "$VNIC" ]] || die "The instance is running but has no VNIC yet."

PRIV_IP="$(ociq oci network private-ip list --vnic-id "$VNIC" \
            --query 'data[?"is-primary"] | [0].id' --raw-output)"
[[ -n "$PRIV_IP" ]] || die "Could not read the primary private IP of ${VNIC}."
ok "primary private IP: ${PRIV_IP}"

log "Reserved public IP '${RESERVED_IP_NAME}'"
RIP="$(ociq oci network public-ip list --compartment-id "$C" --scope REGION --all \
        --query "data[?\"display-name\"=='${RESERVED_IP_NAME}' && \"lifecycle-state\"!='TERMINATED'] | [0].id" \
        --raw-output)"

RIP_PRIV=""
if [[ -n "$RIP" ]]; then
  RIP_PRIV="$(ociq oci network public-ip get --public-ip-id "$RIP" \
               --query 'data."private-ip-id"' --raw-output)"
fi

if [[ -n "$RIP" && "$RIP_PRIV" == "$PRIV_IP" ]]; then
  ok "exists and is already assigned to this instance: ${RIP}"
else
  # Release the ephemeral address, if the instance still has one.
  EPHEMERAL="$(ociq oci network vnic get --vnic-id "$VNIC" \
                --query 'data."public-ip"' --raw-output)"
  if [[ -n "$EPHEMERAL" ]]; then
    INST_AD="$(ociq oci compute instance get --instance-id "$INST" \
                --query 'data."availability-domain"' --raw-output)"
    EPH_ID="$(ociq oci network public-ip list --compartment-id "$C" \
               --scope AVAILABILITY_DOMAIN --availability-domain "$INST_AD" --all \
               --query "data[?\"ip-address\"=='${EPHEMERAL}'] | [0].id" --raw-output)"
    if [[ -n "$EPH_ID" ]]; then
      warn "releasing the ephemeral address ${EPHEMERAL}: the instance's address changes now"
      oci network public-ip delete --public-ip-id "$EPH_ID" --force \
        --wait-for-state TERMINATED >/dev/null 2>&1 || true
      ok "released"
    fi
  fi

  if [[ -n "$RIP" ]]; then
    if [[ -n "$RIP_PRIV" ]]; then
      warn "'${RESERVED_IP_NAME}' is assigned to another private IP (${RIP_PRIV}); moving it here."
      warn "if OCI refuses the move, unassign it first: Console -> Networking -> Reserved public IPs."
    fi
    log "assigning the existing reserved IP"
    oci network public-ip update --public-ip-id "$RIP" \
      --private-ip-id "$PRIV_IP" --wait-for-state ASSIGNED >/dev/null
    ok "assigned: ${RIP}"
  else
    log "creating and assigning the reserved IP"
    RIP="$(oci network public-ip create \
            --compartment-id "$C" \
            --display-name "$RESERVED_IP_NAME" \
            --lifetime RESERVED \
            --private-ip-id "$PRIV_IP" \
            --wait-for-state ASSIGNED \
            --query 'data.id' --raw-output)"
    ok "created: ${RIP}"
  fi
fi

# ---------------------------------------------------------------------------
# 10. Public IP and final state
# ---------------------------------------------------------------------------

log "Reading the public IP"
IP=""
for _ in $(seq 1 20); do
  IP="$(ociq oci network vnic get --vnic-id "$VNIC" \
         --query 'data."public-ip"' --raw-output)"
  [[ -n "$IP" ]] && break
  sleep 5
done
[[ -n "$IP" ]] || die "The reserved IP was assigned but the VNIC reports no public address."
ok "public IP: ${IP} (reserved)"

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
export VNIC=${VNIC}
export PRIV_IP=${PRIV_IP}
export RIP=${RIP}
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
  public IP    ${IP}  reserved as '${RESERVED_IP_NAME}'
  ingress      22 (default)$( [[ "$OPEN_WEB" == "true" ]] && echo ", 80, 443" )

Connect:
  ssh -i ${SSH_KEY} ${SSH_USER}@${IP}

Reload these variables in another session:
  source ${ENV_FILE}

Next step: section 7 — point the wildcard DNS record at ${IP}.

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


## Appendix D — devbox-oci-teardown.sh

Run from Cloud Shell. Deletes everything Appendix A created and nothing else. Section 15.

<!-- file: devbox-oci-teardown.sh -->
```bash
#!/usr/bin/env bash
#
# devbox-oci-teardown.sh
#
# Destroys everything devbox-oci-setup.sh created, in reverse order:
#   instance -> boot volumes -> reserved IP -> subnet -> route rules
#   -> internet gateway -> security list rules -> VCN -> compartment
#
# Every resource is looked up by the same name the setup script uses, so it
# finds what that script would have reused. Nothing is deleted by pattern or
# by sweeping the compartment: an object this script did not create is
# reported as a leftover and left alone.
#
# Usage:
#   DRY_RUN=true bash devbox-oci-teardown.sh    # show what would go, delete nothing
#   bash devbox-oci-teardown.sh                 # asks for confirmation
#   FORCE=true bash devbox-oci-teardown.sh      # no prompt, for scripts
#
# Opt-in extras:
#   KEEP_RESERVED_IP=true     keep the reserved IP, so DNS still resolves to
#                             something and a rebuild reuses the same address
#   DELETE_COMPARTMENT=true   also delete the 'devbox' compartment
#   DELETE_LOCAL=true         also delete ~/.ssh/oci_devbox* and ~/devbox.env
#
# Requires: OCI CLI configured (oci setup config), or run it from Cloud Shell.
#

set -euo pipefail

# ---------------------------------------------------------------------------
# Configuration — every name must match devbox-oci-setup.sh
# ---------------------------------------------------------------------------

REGION="${REGION:-eu-frankfurt-1}"

COMPARTMENT_NAME="devbox"
VCN_NAME="vcn-devbox"
IGW_NAME="igw-devbox"
SUBNET_NAME="subnet-devbox-public"
INSTANCE_NAME="devbox"
RESERVED_IP_NAME="devbox-public-ip"     # created and assigned by the setup script

SSH_KEY="${HOME}/.ssh/oci_devbox"
ENV_FILE="${HOME}/devbox.env"

# Ports the setup script adds to the default security list
WEB_PORTS="80 443"

DRY_RUN="${DRY_RUN:-false}"
FORCE="${FORCE:-false}"
KEEP_RESERVED_IP="${KEEP_RESERVED_IP:-false}"
DELETE_COMPARTMENT="${DELETE_COMPARTMENT:-false}"
DELETE_LOCAL="${DELETE_LOCAL:-false}"

WAIT_MAX=300                            # seconds to keep retrying a delete

export OCI_CLI_REGION="$REGION"

# ---------------------------------------------------------------------------
# Utilities
# ---------------------------------------------------------------------------

RED=$'\033[0;31m'; GRN=$'\033[0;32m'; YLW=$'\033[0;33m'; BLU=$'\033[0;34m'; RST=$'\033[0m'

log()  { printf '%s[%s]%s %s\n' "$BLU" "$(date +%H:%M:%S)" "$RST" "$*"; }
ok()   { printf '%s  ok%s    %s\n' "$GRN" "$RST" "$*"; }
warn() { printf '%s  warn%s  %s\n' "$YLW" "$RST" "$*"; }
die()  { printf '%s  error%s %s\n' "$RED" "$RST" "$*" >&2; exit 1; }
gone() { printf '%s  gone%s  %s\n' "$GRN" "$RST" "$*"; }

ERRFILE="$(mktemp)"
trap 'rm -f "$ERRFILE"' EXIT

LEFTOVERS=()
note_leftover() { LEFTOVERS+=("$1"); }

# An OCI query that may legitimately return nothing; null -> ""
ociq() {
  local out
  out="$("$@" 2>/dev/null || true)"
  [[ "$out" == "null" ]] && out=""
  printf '%s' "$out"
}

# A --query returning a list of scalars, one per line
ocil() {
  ociq "$@" | tr -d '[],"' | sed 's/^[[:space:]]*//; s/[[:space:]]*$//; /^$/d'
}

# Nothing is ever deleted on the strength of a value that is not an OCID: an
# empty result, a JMESPath typo or an error message must not reach a delete.
ocid_only() { grep -E '^ocid1\.[a-z0-9]+\.' || true; }
an_ocid()   { [[ "${1:-}" =~ ^ocid1\.[a-z0-9]+\. ]] && printf '%s' "$1" || printf ''; }

# Deletes with retries: OCI often needs a moment to release a dependency
# (a VNIC keeping a subnet busy, a reserved IP still detaching).
destroy() {                              # destroy <what> <cmd...>
  local what="$1"; shift
  if [[ "$DRY_RUN" == "true" ]]; then
    printf '%s  dry%s   would delete %s\n' "$YLW" "$RST" "$what"
    printf '          %s\n' "$*"
    return 0
  fi
  log "deleting ${what}"
  local waited=0
  while true; do
    if "$@" >/dev/null 2>"$ERRFILE"; then
      gone "$what"
      return 0
    fi
    # --wait-for-state polls the resource after it is gone, so a 404 here is
    # success, not a failure. Anything genuinely left shows up in the final scan.
    if grep -qiE 'NotAuthorizedOrNotFound|status: 404|does not exist|no longer exists' "$ERRFILE"; then
      gone "$what"
      return 0
    fi
    if (( waited >= WAIT_MAX )); then
      warn "could not delete ${what} after ${waited}s:"
      sed 's/^/         /' "$ERRFILE" >&2
      note_leftover "${what} — delete it by hand"
      return 1
    fi
    sleep 10
    waited=$((waited + 10))
    log "  still busy, retrying ${what} (${waited}s)"
  done
}

# ---------------------------------------------------------------------------
# 0. Pre-flight
# ---------------------------------------------------------------------------

command -v oci >/dev/null 2>&1 || die "oci CLI not found. Install it or use Cloud Shell."
command -v python3 >/dev/null 2>&1 || die "python3 not found (needed to edit the security list)."

log "Checking credentials and region ${REGION}"
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
  die "REGION=${REGION} is not the tenancy home region (${HOME_REGION}). The devbox lives there: re-run with REGION=${HOME_REGION}."
fi

# ---------------------------------------------------------------------------
# 1. Find everything
# ---------------------------------------------------------------------------

log "Compartment '${COMPARTMENT_NAME}'"
C="$(ociq oci iam compartment list --compartment-id "$TENANCY" --all \
      --query "data[?name=='${COMPARTMENT_NAME}' && \"lifecycle-state\"=='ACTIVE'].id | [0]" \
      --raw-output)"

if [[ -z "$C" && -f "$ENV_FILE" ]]; then
  # Compartment renamed or already gone: fall back to the OCID the setup wrote
  C="$(grep -E '^export C=' "$ENV_FILE" 2>/dev/null | head -n1 | cut -d= -f2- || true)"
  [[ -n "$C" ]] && warn "not found by name; using C=${C} from ${ENV_FILE}"
fi
C="$(an_ocid "$C")"
[[ -n "$C" ]] || die "No '${COMPARTMENT_NAME}' compartment in this tenancy. Nothing to tear down."
ok "compartment: ${C}"

mapfile -t ADS < <(ocil oci iam availability-domain list --compartment-id "$C" \
                     --query 'data[*].name' --raw-output)

log "Instance '${INSTANCE_NAME}'"
mapfile -t INSTANCES < <(ocil oci compute instance list --compartment-id "$C" --all \
    --display-name "$INSTANCE_NAME" \
    --query "data[?\"lifecycle-state\"!='TERMINATED' && \"lifecycle-state\"!='TERMINATING'].id" \
    --raw-output | ocid_only)
for i in "${INSTANCES[@]:-}"; do [[ -n "$i" ]] && ok "instance: ${i}"; done
[[ ${#INSTANCES[@]} -eq 0 ]] && ok "none"

# Instances the setup script did not create: never touched, only reported
mapfile -t OTHER_INSTANCES < <(ocil oci compute instance list --compartment-id "$C" --all \
    --query "data[?\"lifecycle-state\"!='TERMINATED' && \"lifecycle-state\"!='TERMINATING' && \"display-name\"!='${INSTANCE_NAME}'].join(' ', [\"display-name\", id])" \
    --raw-output)

log "VCN '${VCN_NAME}'"
VCN="$(an_ocid "$(ociq oci network vcn list --compartment-id "$C" --display-name "$VCN_NAME" --all \
        --query "data[?\"lifecycle-state\"!='TERMINATED'] | [0].id" --raw-output)")"
RT=""; SL=""; SUBNETS=(); IGWS=()
if [[ -n "$VCN" ]]; then
  ok "vcn: ${VCN}"
  RT="$(an_ocid "$(ociq oci network vcn get --vcn-id "$VCN" --query 'data."default-route-table-id"' --raw-output)")"
  SL="$(an_ocid "$(ociq oci network vcn get --vcn-id "$VCN" --query 'data."default-security-list-id"' --raw-output)")"
  mapfile -t SUBNETS < <(ocil oci network subnet list --compartment-id "$C" --vcn-id "$VCN" --all \
      --query "data[?\"lifecycle-state\"!='TERMINATED'].id" --raw-output | ocid_only)
  mapfile -t IGWS < <(ocil oci network internet-gateway list --compartment-id "$C" --vcn-id "$VCN" --all \
      --query "data[?\"lifecycle-state\"!='TERMINATED'].id" --raw-output | ocid_only)
  for s in "${SUBNETS[@]:-}"; do [[ -n "$s" ]] && ok "subnet: ${s}"; done
  for g in "${IGWS[@]:-}"; do [[ -n "$g" ]] && ok "internet gateway: ${g}"; done
else
  ok "none"
fi

log "Reserved public IP '${RESERVED_IP_NAME}'"
mapfile -t RIPS < <(ocil oci network public-ip list --compartment-id "$C" --scope REGION --all \
    --query "data[?\"lifecycle-state\"!='TERMINATED' && \"display-name\"=='${RESERVED_IP_NAME}'].id" \
    --raw-output | ocid_only)
for r in "${RIPS[@]:-}"; do
  [[ -n "$r" ]] || continue
  ok "reserved IP: $(ociq oci network public-ip get --public-ip-id "$r" --query 'data."ip-address"' --raw-output) (${r})"
done
[[ ${#RIPS[@]} -eq 0 ]] && ok "none"

log "Boot volumes"
BVOLS=()
for ad in "${ADS[@]}"; do
  mapfile -t -O "${#BVOLS[@]}" BVOLS < <(ocil oci bv boot-volume list \
      --compartment-id "$C" --availability-domain "$ad" --all \
      --query "data[?\"lifecycle-state\"!='TERMINATED'].id" --raw-output | ocid_only)
done
for b in "${BVOLS[@]:-}"; do [[ -n "$b" ]] && ok "boot volume: ${b}"; done
[[ ${#BVOLS[@]} -eq 0 ]] && ok "none"

# ---------------------------------------------------------------------------
# 2. Confirm
# ---------------------------------------------------------------------------

TOTAL=$(( ${#INSTANCES[@]} + ${#SUBNETS[@]} + ${#IGWS[@]} + ${#BVOLS[@]} ))
[[ -n "$VCN" ]] && TOTAL=$((TOTAL + 1))
if [[ "$TOTAL" -eq 0 ]]; then
  ok "nothing left from devbox-oci-setup.sh in ${REGION}"
  [[ "$DELETE_COMPARTMENT" != "true" ]] && exit 0
fi

cat <<EOF

${RED}This deletes, permanently:${RST}
  instance          ${#INSTANCES[@]}   (boot volume included — everything in /srv/dev goes with it)
  boot volumes      ${#BVOLS[@]}
  subnet            ${#SUBNETS[@]}
  internet gateway  ${#IGWS[@]}
  vcn               $( [[ -n "$VCN" ]] && echo 1 || echo 0 )
  reserved IP       $( [[ "$KEEP_RESERVED_IP" == "true" ]] && echo "0 (kept; the DNS record still resolves)" || echo "${#RIPS[@]}   — the address is released for good" )
  compartment       $( [[ "$DELETE_COMPARTMENT" == "true" ]] && echo 1 || echo "0 (kept; DELETE_COMPARTMENT=true to remove)" )
  local files       $( [[ "$DELETE_LOCAL" == "true" ]] && echo "${SSH_KEY}, ${ENV_FILE}" || echo "0 (kept; DELETE_LOCAL=true to remove)" )

  region            ${REGION}
EOF

# A compartment cannot be deleted while it still holds anything
if [[ "$DELETE_COMPARTMENT" == "true" && "$KEEP_RESERVED_IP" == "true" && ${#RIPS[@]} -gt 0 ]]; then
  warn "KEEP_RESERVED_IP=true leaves the IP inside the compartment, so the compartment deletion will be refused."
  warn "keep one or the other, not both."
fi

if [[ "$DRY_RUN" == "true" ]]; then
  warn "DRY_RUN=true — nothing will be deleted"
elif [[ "$FORCE" != "true" ]]; then
  [[ -t 0 ]] || die "not a terminal: re-run with FORCE=true if you mean it."
  printf '\nType %s to confirm: ' "$COMPARTMENT_NAME"
  read -r ANSWER
  [[ "$ANSWER" == "$COMPARTMENT_NAME" ]] || die "aborted, nothing was deleted."
fi
echo

# ---------------------------------------------------------------------------
# 3. Instance
# ---------------------------------------------------------------------------

for inst in "${INSTANCES[@]:-}"; do
  [[ -n "$inst" ]] || continue
  destroy "instance ${inst}" \
    oci compute instance terminate --instance-id "$inst" \
      --preserve-boot-volume false --force --wait-for-state TERMINATED
done

# ---------------------------------------------------------------------------
# 4. Boot volumes left detached
# ---------------------------------------------------------------------------

if [[ "$DRY_RUN" != "true" && ${#INSTANCES[@]} -gt 0 ]]; then
  BVOLS=()
  for ad in "${ADS[@]}"; do
    mapfile -t -O "${#BVOLS[@]}" BVOLS < <(ocil oci bv boot-volume list \
        --compartment-id "$C" --availability-domain "$ad" --all \
        --query "data[?\"lifecycle-state\"!='TERMINATED'].id" --raw-output)
  done
fi
for bv in "${BVOLS[@]:-}"; do
  [[ -n "$bv" ]] || continue
  destroy "boot volume ${bv}" \
    oci bv boot-volume delete --boot-volume-id "$bv" --force --wait-for-state TERMINATED
done

# ---------------------------------------------------------------------------
# 5. Reserved public IP
# ---------------------------------------------------------------------------

# Terminating the instance unassigns it; a reserved IP that is still assigned
# cannot be deleted, which is what destroy()'s retries are waiting out here.
if [[ "$KEEP_RESERVED_IP" == "true" ]]; then
  [[ ${#RIPS[@]} -gt 0 ]] && warn "reserved IP kept: the wildcard DNS record still points at it, and the next devbox-oci-setup.sh run re-assigns it to the new instance."
else
  for rip in "${RIPS[@]:-}"; do
    [[ -n "$rip" ]] || continue
    destroy "reserved public IP ${rip}" \
      oci network public-ip delete --public-ip-id "$rip" --force
  done
fi

# ---------------------------------------------------------------------------
# 6. Subnet
# ---------------------------------------------------------------------------

# A terminated instance releases its VNIC a few seconds later; destroy() waits.
for sn in "${SUBNETS[@]:-}"; do
  [[ -n "$sn" ]] || continue
  destroy "subnet ${sn}" \
    oci network subnet delete --subnet-id "$sn" --force --wait-for-state TERMINATED
done

# ---------------------------------------------------------------------------
# 7. Route rules, then the internet gateway
# ---------------------------------------------------------------------------

if [[ -n "$RT" && ${#IGWS[@]} -gt 0 ]]; then
  # The gateway cannot go while a route rule still points at it.
  if [[ "$DRY_RUN" == "true" ]]; then
    printf '%s  dry%s   would clear the route rules of %s\n' "$YLW" "$RST" "$RT"
  else
    log "clearing route rules on ${RT}"
    oci network route-table update --rt-id "$RT" --force --route-rules '[]' >/dev/null \
      && ok "route table emptied" || warn "could not empty the route table"
  fi
fi

for igw in "${IGWS[@]:-}"; do
  [[ -n "$igw" ]] || continue
  destroy "internet gateway ${igw}" \
    oci network internet-gateway delete --ig-id "$igw" --force --wait-for-state TERMINATED
done

# ---------------------------------------------------------------------------
# 8. Security list ingress added by the setup script
# ---------------------------------------------------------------------------

# The default security list is deleted with the VCN, so this only matters when
# the VCN survives — a partial teardown, or a re-run with the VCN kept. It
# removes the 80/443 rules and nothing else.
if [[ -n "$SL" ]]; then
  EXISTING="$(ociq oci network security-list get --security-list-id "$SL" \
               --query 'data."ingress-security-rules"' --output json)"
  KEPT="$(python3 - "$EXISTING" "$WEB_PORTS" <<'PY'
import json, sys
existing = json.loads(sys.argv[1] or "[]")
ports = {int(p) for p in sys.argv[2].split()}

def conv(r):
    out = {}
    for k, v in r.items():
        if v is None:
            continue
        ck = ''.join(w if i == 0 else w.capitalize() for i, w in enumerate(k.split('-')))
        out[ck] = conv(v) if isinstance(v, dict) else v
    return out

def is_web(r):
    rng = (r.get("tcp-options") or {}).get("destination-port-range") or {}
    return (r.get("protocol") == "6" and rng.get("min") == rng.get("max")
            and rng.get("min") in ports)

kept = [r for r in existing if not is_web(r)]
if len(kept) != len(existing):
    print(json.dumps([conv(r) for r in kept]))
PY
)"
  if [[ -z "$KEPT" ]]; then
    ok "security list has no 80/443 rule to remove"
  elif [[ "$DRY_RUN" == "true" ]]; then
    printf '%s  dry%s   would remove the 80/443 ingress rules from %s\n' "$YLW" "$RST" "$SL"
  else
    log "removing the 80/443 ingress rules"
    oci network security-list update --security-list-id "$SL" --force \
      --ingress-security-rules "$KEPT" >/dev/null && ok "removed" || warn "could not update the security list"
  fi
fi

# ---------------------------------------------------------------------------
# 9. VCN
# ---------------------------------------------------------------------------

if [[ -n "$VCN" ]]; then
  destroy "vcn ${VCN}" \
    oci network vcn delete --vcn-id "$VCN" --force --wait-for-state TERMINATED
fi

# ---------------------------------------------------------------------------
# 10. Compartment
# ---------------------------------------------------------------------------

if [[ "$DELETE_COMPARTMENT" == "true" ]]; then
  if [[ "$DRY_RUN" == "true" ]]; then
    printf '%s  dry%s   would delete compartment %s\n' "$YLW" "$RST" "$C"
  else
    log "deleting compartment ${C}"
    if oci iam compartment delete --compartment-id "$C" --force >/dev/null 2>"$ERRFILE"; then
      ok "deletion accepted — it runs asynchronously and takes a few minutes"
      warn "the compartment stays visible in DELETED state; check it before re-running the setup"
    else
      sed 's/^/         /' "$ERRFILE" >&2
      warn "compartment not deleted: it still holds something. See the leftovers below."
      note_leftover "compartment ${COMPARTMENT_NAME} (${C})"
    fi
  fi
else
  ok "compartment kept (empty). Re-running devbox-oci-setup.sh reuses it."
fi

# ---------------------------------------------------------------------------
# 11. Local files
# ---------------------------------------------------------------------------

if [[ "$DELETE_LOCAL" == "true" ]]; then
  if [[ "$DRY_RUN" == "true" ]]; then
    printf '%s  dry%s   would delete %s, %s.pub, %s\n' "$YLW" "$RST" "$SSH_KEY" "$SSH_KEY" "$ENV_FILE"
  else
    rm -f "$SSH_KEY" "${SSH_KEY}.pub" "$ENV_FILE"
    gone "local key and ${ENV_FILE}"
  fi
else
  [[ -f "$SSH_KEY" ]] && ok "local SSH key kept: ${SSH_KEY}"
fi

# ---------------------------------------------------------------------------
# 12. What is left
# ---------------------------------------------------------------------------

# Anything still in the compartment that this script did not create. These are
# what blocks a compartment deletion, so they are worth naming explicitly.
if [[ "$DRY_RUN" != "true" ]]; then
  for line in "${OTHER_INSTANCES[@]:-}"; do
    [[ -n "$line" ]] && note_leftover "instance not created by the setup script: ${line}"
  done
  mapfile -t VOLS < <(ocil oci bv volume list --compartment-id "$C" --all \
      --query "data[?\"lifecycle-state\"!='TERMINATED'].join(' ', [\"display-name\", id])" --raw-output)
  for line in "${VOLS[@]:-}"; do
    [[ -n "$line" ]] && note_leftover "block volume: ${line}"
  done
  mapfile -t OTHER_VCNS < <(ocil oci network vcn list --compartment-id "$C" --all \
      --query "data[?\"lifecycle-state\"!='TERMINATED'].join(' ', [\"display-name\", id])" --raw-output)
  for line in "${OTHER_VCNS[@]:-}"; do
    [[ -n "$line" ]] && note_leftover "vcn: ${line}"
  done
  for r in "${RIPS[@]:-}"; do
    [[ -n "$r" && "$KEEP_RESERVED_IP" == "true" ]] && note_leftover "reserved public IP ${RESERVED_IP_NAME} (${r}) — kept on purpose"
  done
fi

echo
if [[ "$DRY_RUN" == "true" ]]; then
  printf '%sDry run finished. Nothing was deleted.%s\n' "$YLW" "$RST"
elif [[ ${#LEFTOVERS[@]} -eq 0 ]]; then
  printf '%sTeardown complete.%s Nothing from devbox-oci-setup.sh is left in %s.\n' "$GRN" "$RST" "$REGION"
else
  printf '%sTeardown finished, with things still in the compartment:%s\n' "$YLW" "$RST"
  printf '  - %s\n' "${LEFTOVERS[@]}"
  printf '\nDelete those in the Console if you want the compartment gone too.\n'
fi

cat <<EOF

Also worth checking, none of it created by devbox-oci-setup.sh:
  - the wildcard DNS record, now pointing at an address you no longer own
  - the tunnel names, still registered to your GitHub account and still counting
    against its limit of 10. Release them in any VS Code client: Remote Explorer
    -> right-click the machine -> Unregister
  - the devbox SSH key on GitHub (Settings -> SSH and GPG keys)
  - the budget alert, if the tenancy has nothing left to watch
EOF
```
