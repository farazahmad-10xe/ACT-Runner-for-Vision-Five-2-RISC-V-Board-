#!/usr/bin/env bash
set -euo pipefail

repo_root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
sanity_flash_staging="/home/lpt-10xe/jenkins-hardware-staging/vf2-privileged-sanity"
weekly_flash_staging="/home/lpt-10xe/jenkins-hardware-staging/vf2-privileged-weekly"
bpif3_flash_staging="/home/lpt-10xe/jenkins-hardware-staging/bpif3-privileged-weekly"

if [[ "$(id -u)" -ne 0 ]]; then
  exec sudo bash "$0" "$@"
fi

install -d -o lpt-10xe -g lpt-10xe -m 0755 "$sanity_flash_staging"
install -d -o lpt-10xe -g lpt-10xe -m 0755 "$weekly_flash_staging"
install -d -o lpt-10xe -g lpt-10xe -m 0755 "$bpif3_flash_staging"

cat > /etc/sudoers.d/jenkins-vf2-hardware <<EOF
lpt-10xe ALL=(root) NOPASSWD: $repo_root/vf2_act_flash.sh --image $repo_root/cert_harness/build/vf2_jh7110/ACT_PRIV_M_OWN_ENV/sd_tail_pack/boot_image.bin --sd-dev /dev/sda
lpt-10xe ALL=(root) NOPASSWD: $repo_root/write_pack_to_sd_tail.sh $repo_root/cert_harness/build/vf2_jh7110/ACT_PRIV_M_OWN_ENV/sd_tail_pack/act_pack.bin /dev/sda
lpt-10xe ALL=(root) NOPASSWD: $repo_root/vf2_act_flash.sh --image $sanity_flash_staging/boot_image.bin --sd-dev /dev/sda
lpt-10xe ALL=(root) NOPASSWD: $repo_root/write_pack_to_sd_tail.sh $sanity_flash_staging/act_pack.bin /dev/sda
lpt-10xe ALL=(root) NOPASSWD: $repo_root/vf2_act_flash.sh --image $weekly_flash_staging/boot_image.bin --sd-dev /dev/sda
lpt-10xe ALL=(root) NOPASSWD: $repo_root/write_pack_to_sd_tail.sh $weekly_flash_staging/act_pack.bin /dev/sda
lpt-10xe ALL=(root) NOPASSWD: $repo_root/bpif3_act_flash.sh --image $bpif3_flash_staging/boot_image.bin --sd-dev /dev/sda
lpt-10xe ALL=(root) NOPASSWD: $repo_root/write_pack_to_sd_tail.sh $bpif3_flash_staging/act_pack.bin /dev/sda
EOF
chmod 0440 /etc/sudoers.d/jenkins-vf2-hardware
visudo -cf /etc/sudoers.d/jenkins-vf2-hardware

echo "Installed exact VF2 Jenkins flash permissions."
echo "Sanity staging: $sanity_flash_staging"
echo "Weekly staging: $weekly_flash_staging"
echo "BPI-F3 weekly staging: $bpif3_flash_staging"
