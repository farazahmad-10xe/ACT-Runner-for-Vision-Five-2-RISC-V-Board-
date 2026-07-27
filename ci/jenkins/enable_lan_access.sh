#!/usr/bin/env bash
set -euo pipefail

jenkins_home="${JENKINS_HOME:-/home/lpt-10xe/.jenkins-vf2}"
jenkins_port="${JENKINS_PORT:-8080}"
lan_ip="${JENKINS_LAN_IP:-$(hostname -I | awk '{print $1}')}"
public_url="${JENKINS_PUBLIC_URL:-http://${lan_ip}:${jenkins_port}/}"

if [[ "$(id -u)" -ne 0 ]]; then
  exec sudo --preserve-env=JENKINS_HOME,JENKINS_PORT,JENKINS_LAN_IP,JENKINS_PUBLIC_URL \
    bash "$0" "$@"
fi

if [[ -z "$lan_ip" ]]; then
  echo "Unable to determine a LAN address. Set JENKINS_LAN_IP explicitly." >&2
  exit 1
fi

install -d -m 0755 /etc/systemd/system/jenkins.service.d
cat > /etc/systemd/system/jenkins.service.d/zz-lan.conf <<EOF
[Service]
Environment="JENKINS_LISTEN_ADDRESS=0.0.0.0"
Environment="JENKINS_PORT=$jenkins_port"
Environment="JENKINS_PUBLIC_URL=$public_url"
EOF

install -d -o lpt-10xe -g lpt-10xe -m 0700 "$jenkins_home/init.groovy.d"
cat > "$jenkins_home/init.groovy.d/99-lan-url.groovy" <<'GROOVY'
import jenkins.model.JenkinsLocationConfiguration

def location = JenkinsLocationConfiguration.get()
def publicUrl = System.getenv("JENKINS_PUBLIC_URL")
if (publicUrl && location.url != publicUrl) {
  location.setUrl(publicUrl)
  location.save()
}
GROOVY
chown lpt-10xe:lpt-10xe "$jenkins_home/init.groovy.d/99-lan-url.groovy"
chmod 0600 "$jenkins_home/init.groovy.d/99-lan-url.groovy"

# If UFW is active, admit only the directly connected LAN rather than opening
# Jenkins to every network interface reachable by this host.
if command -v ufw >/dev/null 2>&1 && ufw status | grep -q '^Status: active'; then
  lan_iface="$(ip -4 route get 1.1.1.1 | awk '{for (i=1;i<=NF;i++) if ($i=="dev") {print $(i+1); exit}}')"
  lan_cidr="$(ip -4 route show dev "$lan_iface" scope link | awk '$1 ~ /^[0-9]+\./ {print $1; exit}')"
  if [[ -n "$lan_cidr" ]]; then
    ufw allow from "$lan_cidr" to any port "$jenkins_port" proto tcp
  else
    echo "UFW is active, but the LAN subnet could not be determined." >&2
    echo "Add a rule for TCP port $jenkins_port from your Wi-Fi subnet manually." >&2
    exit 1
  fi
fi

systemctl daemon-reload
systemctl restart jenkins

for _ in $(seq 1 90); do
  if curl -fsS "http://127.0.0.1:${jenkins_port}/login" >/dev/null 2>&1; then
    echo "Jenkins LAN access is ready: $public_url"
    exit 0
  fi
  sleep 2
done

echo "Jenkins did not become ready after restart." >&2
journalctl -u jenkins.service -n 100 --no-pager >&2
exit 1
