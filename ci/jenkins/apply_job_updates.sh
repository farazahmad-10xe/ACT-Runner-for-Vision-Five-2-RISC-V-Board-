#!/usr/bin/env bash
set -euo pipefail

repo_root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
jenkins_home="${JENKINS_HOME:-/home/lpt-10xe/.jenkins-vf2}"
plugin_manager_version="2.13.2"
plugin_manager_jar="/tmp/jenkins-plugin-manager-${plugin_manager_version}.jar"

if [[ "$(id -u)" -ne 0 ]]; then
  exec sudo --preserve-env=JENKINS_HOME bash "$0" "$@"
fi

if [[ ! -s "$plugin_manager_jar" ]]; then
  wget -qO "$plugin_manager_jar" \
    "https://github.com/jenkinsci/plugin-installation-manager-tool/releases/download/${plugin_manager_version}/jenkins-plugin-manager-${plugin_manager_version}.jar"
fi

systemctl stop jenkins
trap 'systemctl start jenkins >/dev/null 2>&1 || true' EXIT

java -jar "$plugin_manager_jar" \
  --war /usr/share/java/jenkins.war \
  --plugin-file "$repo_root/ci/jenkins/plugins.txt" \
  --plugin-download-directory "$jenkins_home/plugins"

python3 - \
  "$repo_root/Jenkinsfile.sanity" \
  "$repo_root/ci/jenkins/job-config-sanity.xml" \
  /tmp/vf2-sanity-job.xml \
  "$repo_root/Jenkinsfile" \
  "$repo_root/ci/jenkins/job-config.xml" \
  /tmp/vf2-weekly-job.xml \
  "$repo_root/Jenkinsfile.act-update-validation" \
  "$repo_root/ci/jenkins/job-config-act-update.xml" \
  /tmp/vf2-act-update-job.xml <<'PY'
import html
import pathlib
import sys
import xml.etree.ElementTree as ET

for jenkins_path, template_path, output_path in (
    (sys.argv[1], sys.argv[2], sys.argv[3]),
    (sys.argv[4], sys.argv[5], sys.argv[6]),
    (sys.argv[7], sys.argv[8], sys.argv[9]),
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
install -o lpt-10xe -g lpt-10xe -m 0644 \
  /tmp/vf2-sanity-job.xml "$jenkins_home/jobs/vf2-privileged-sanity/config.xml"
install -o lpt-10xe -g lpt-10xe -m 0644 \
  /tmp/vf2-weekly-job.xml "$jenkins_home/jobs/vf2-privileged-weekly/config.xml"
install -o lpt-10xe -g lpt-10xe -m 0644 \
  /tmp/vf2-act-update-job.xml "$jenkins_home/jobs/vf2-act-update-validation/config.xml"
chown -R lpt-10xe:lpt-10xe "$jenkins_home/plugins"

systemctl start jenkins
trap - EXIT
for _ in $(seq 1 90); do
  if curl -fsS http://127.0.0.1:8080/login >/dev/null 2>&1; then
    echo "Jenkins sanity, weekly hardware, and ACT update-validation jobs updated successfully."
    echo "The next build will show the Result Summary link and downloadable artifacts."
    exit 0
  fi
  sleep 2
done

echo "Jenkins did not become ready after applying the job update." >&2
journalctl -u jenkins.service -n 100 --no-pager >&2
exit 1
