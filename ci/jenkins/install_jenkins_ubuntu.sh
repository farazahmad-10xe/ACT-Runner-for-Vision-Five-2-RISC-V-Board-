#!/usr/bin/env bash
set -euo pipefail

repo_root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
jenkins_home="/home/lpt-10xe/.jenkins-vf2"
plugin_manager_version="2.13.2"
plugin_manager_jar="/tmp/jenkins-plugin-manager-${plugin_manager_version}.jar"
jenkins_port="${JENKINS_PORT:-8080}"
jenkins_listen_address="${JENKINS_LISTEN_ADDRESS:-0.0.0.0}"
lan_ip="$(hostname -I | awk '{print $1}')"
jenkins_public_url="${JENKINS_PUBLIC_URL:-http://${lan_ip}:${jenkins_port}/}"
jenkins_health_url="http://127.0.0.1:${jenkins_port}"

if [[ "$(id -u)" -ne 0 ]]; then
  exec sudo --preserve-env=PATH,JENKINS_PORT,JENKINS_LISTEN_ADDRESS,JENKINS_PUBLIC_URL \
    bash "$0" "$@"
fi

install -d -m 0755 /etc/apt/keyrings
apt-get update
apt-get install -y fontconfig openjdk-21-jre wget curl ca-certificates gnupg
wget -qO /etc/apt/keyrings/jenkins-keyring.asc \
  https://pkg.jenkins.io/debian-stable/jenkins.io-2026.key
printf '%s\n' 'deb [signed-by=/etc/apt/keyrings/jenkins-keyring.asc] https://pkg.jenkins.io/debian-stable binary/' \
  > /etc/apt/sources.list.d/jenkins.list
apt-get update
apt-get install -y jenkins
systemctl stop jenkins || true

install -d -o lpt-10xe -g lpt-10xe -m 0700 \
  "$jenkins_home" \
  "$jenkins_home/plugins" \
  "$jenkins_home/init.groovy.d" \
  "$jenkins_home/war"

wget -qO "$plugin_manager_jar" \
  "https://github.com/jenkinsci/plugin-installation-manager-tool/releases/download/${plugin_manager_version}/jenkins-plugin-manager-${plugin_manager_version}.jar"
java -jar "$plugin_manager_jar" \
  --war /usr/share/java/jenkins.war \
  --plugin-file "$repo_root/ci/jenkins/plugins.txt" \
  --plugin-download-directory "$jenkins_home/plugins"
chown -R lpt-10xe:lpt-10xe "$jenkins_home"
chmod 0700 "$jenkins_home" "$jenkins_home/plugins" "$jenkins_home/init.groovy.d" "$jenkins_home/war"

install -d -m 0755 /etc/systemd/system/jenkins.service.d
cat > /etc/systemd/system/jenkins.service.d/vf2.conf <<EOF
[Service]
User=lpt-10xe
Group=lpt-10xe
Environment="JENKINS_HOME=$jenkins_home"
Environment="JENKINS_WEBROOT=$jenkins_home/war"
Environment="JAVA_OPTS=-Djava.awt.headless=true -Djenkins.install.runSetupWizard=false"
Environment="JENKINS_LISTEN_ADDRESS=$jenkins_listen_address"
Environment="JENKINS_PORT=$jenkins_port"
Environment="JENKINS_PUBLIC_URL=$jenkins_public_url"
WorkingDirectory=$repo_root
EOF

cat > "$jenkins_home/init.groovy.d/10-vf2-bootstrap.groovy" <<'GROOVY'
import hudson.security.HudsonPrivateSecurityRealm
import hudson.security.FullControlOnceLoggedInAuthorizationStrategy
import jenkins.model.Jenkins
import jenkins.model.JenkinsLocationConfiguration

def j = Jenkins.get()
def location = JenkinsLocationConfiguration.get()
def publicUrl = System.getenv("JENKINS_PUBLIC_URL") ?: "http://127.0.0.1:8080/"
if (location.url != publicUrl) {
  location.setUrl(publicUrl)
  location.save()
}
if (!(j.securityRealm instanceof HudsonPrivateSecurityRealm)) {
  def realm = new HudsonPrivateSecurityRealm(false)
  def passwordFile = new File(j.rootDir, "secrets/vf2AdminPassword")
  passwordFile.parentFile.mkdirs()
  def password = UUID.randomUUID().toString() + UUID.randomUUID().toString()
  passwordFile.text = password + "\n"
  passwordFile.setReadable(false, false)
  passwordFile.setReadable(true, true)
  realm.createAccount("vf2admin", password)
  j.securityRealm = realm
  def auth = new FullControlOnceLoggedInAuthorizationStrategy()
  auth.setAllowAnonymousRead(false)
  j.authorizationStrategy = auth
  j.save()
}
GROOVY
chown lpt-10xe:lpt-10xe "$jenkins_home/init.groovy.d/10-vf2-bootstrap.groovy"
chmod 0600 "$jenkins_home/init.groovy.d/10-vf2-bootstrap.groovy"

# Permit only fixed helper, artifact, and device paths. Raw dd, blockdev, and
# umount are deliberately not exposed to Jenkins.
bash "$repo_root/ci/jenkins/install_vf2_sudoers.sh"

systemctl daemon-reload

python3 - \
  "$repo_root/Jenkinsfile.sanity" \
  "$repo_root/ci/jenkins/job-config-sanity.xml" \
  /tmp/vf2-sanity-job.xml \
  "$repo_root/Jenkinsfile" \
  "$repo_root/ci/jenkins/job-config.xml" \
  /tmp/vf2-weekly-job.xml \
  "$repo_root/Jenkinsfile.act-update-validation" \
  "$repo_root/ci/jenkins/job-config-act-update.xml" \
  /tmp/vf2-act-update-job.xml \
  "$repo_root/Jenkinsfile.dashboard" \
  "$repo_root/ci/jenkins/job-config-dashboard.xml" \
  /tmp/vf2-dashboard-job.xml <<'PY'
import html
import pathlib
import sys
import xml.etree.ElementTree as ET

for jenkins_path, template_path, output_path in (
    (sys.argv[1], sys.argv[2], sys.argv[3]),
    (sys.argv[4], sys.argv[5], sys.argv[6]),
    (sys.argv[7], sys.argv[8], sys.argv[9]),
    (sys.argv[10], sys.argv[11], sys.argv[12]),
):
    jenkinsfile = pathlib.Path(jenkins_path).read_text(encoding="utf-8")
    template = pathlib.Path(template_path).read_text(encoding="utf-8")
    rendered = template.replace("__JENKINSFILE__", html.escape(jenkinsfile))
    ET.fromstring(rendered)
    pathlib.Path(output_path).write_text(rendered, encoding="utf-8")
PY

install -d -o lpt-10xe -g lpt-10xe -m 0755 "$jenkins_home/jobs/vf2-privileged-sanity"
install -d -o lpt-10xe -g lpt-10xe -m 0755 "$jenkins_home/jobs/vf2-privileged-weekly"
install -d -o lpt-10xe -g lpt-10xe -m 0755 "$jenkins_home/jobs/vf2-act-update-validation"
install -d -o lpt-10xe -g lpt-10xe -m 0755 "$jenkins_home/jobs/vf2-validation-dashboard"
install -o lpt-10xe -g lpt-10xe -m 0644 \
  /tmp/vf2-sanity-job.xml "$jenkins_home/jobs/vf2-privileged-sanity/config.xml"
install -o lpt-10xe -g lpt-10xe -m 0644 \
  /tmp/vf2-weekly-job.xml "$jenkins_home/jobs/vf2-privileged-weekly/config.xml"
install -o lpt-10xe -g lpt-10xe -m 0644 \
  /tmp/vf2-act-update-job.xml "$jenkins_home/jobs/vf2-act-update-validation/config.xml"
install -o lpt-10xe -g lpt-10xe -m 0644 \
  /tmp/vf2-dashboard-job.xml "$jenkins_home/jobs/vf2-validation-dashboard/config.xml"
rm -f "$jenkins_home/secrets/cli-auth"

systemctl enable --now jenkins

ready=false
for _ in $(seq 1 90); do
  if curl -fsS "$jenkins_health_url/login" >/dev/null 2>&1; then
    ready=true
    break
  fi
  sleep 2
done
if [[ "$ready" != true ]]; then
  echo "Jenkins did not become ready at $jenkins_health_url" >&2
  journalctl -u jenkins.service -n 100 --no-pager >&2 || true
  exit 1
fi

test -f "$jenkins_home/secrets/vf2AdminPassword"
test -f "$jenkins_home/jobs/vf2-privileged-sanity/config.xml"
test -f "$jenkins_home/jobs/vf2-privileged-weekly/config.xml"
test -f "$jenkins_home/jobs/vf2-act-update-validation/config.xml"
test -f "$jenkins_home/jobs/vf2-validation-dashboard/config.xml"

echo "Jenkins is running at $jenkins_public_url"
echo "Job: vf2-privileged-sanity"
echo "Job: vf2-privileged-weekly"
echo "Job: vf2-act-update-validation"
echo "Job: vf2-validation-dashboard"
echo "Admin user: vf2admin"
echo "Read the password locally with: sudo cat $jenkins_home/secrets/vf2AdminPassword"
