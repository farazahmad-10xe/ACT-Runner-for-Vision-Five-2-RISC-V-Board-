pipeline {
    agent {
        node {
            label 'built-in'
            customWorkspace '/home/lpt-10xe/vf2_mmode_fw_Final_version_Verified'
        }
    }

    options {
        disableConcurrentBuilds()
        buildDiscarder(logRotator(numToKeepStr: '8', artifactNumToKeepStr: '4'))
        timeout(time: 14, unit: 'HOURS')
        timestamps()
        skipDefaultCheckout(true)
    }

    parameters {
        string(name: 'ACT_REVISION',
            defaultValue: '0683245155d659437be40d353cedf26fb0d56f1c',
            description: 'Exact reviewed VF2 ACT commit required for this hardware run. Jenkins will not pull or silently change it.')
        booleanParam(name: 'REGENERATE_TESTS', defaultValue: true,
            description: 'Regenerate testgen-backed privileged suites before ACT/Sail execution.')
        booleanParam(name: 'RUN_SPIKE', defaultValue: true,
            description: 'Run each packed hardware ELF on Spike before the board stage.')
        booleanParam(name: 'RUN_HARDWARE', defaultValue: true,
            description: 'Flash the VF2 SD card, run the board, and generate ACT Agent reports.')
        string(name: 'SD_DEV', defaultValue: '/dev/sda', description: 'Dedicated VF2 SD-card device.')
        string(name: 'SERIAL_DEV', defaultValue: '/dev/ttyUSB0', description: 'VF2 UART device.')
        string(name: 'CAPTURE_TIMEOUT', defaultValue: '10800', description: 'Maximum VF2 UART capture time in seconds.')
    }

    environment {
        REPO_ROOT = '/home/lpt-10xe/vf2_mmode_fw_Final_version_Verified'
        SAIL_BIN = '/home/lpt-10xe/riscv-sail-0.13/bin/sail_riscv_sim'
        SAIL_EXPECTED_VERSION = '0.13'
        PATH = '/home/lpt-10xe/.local/bin:/home/lpt-10xe/riscv64/bin:/home/lpt-10xe/riscv-sail-0.13/bin:/home/lpt-10xe/.rbenv/shims:/home/lpt-10xe/.rbenv/bin:/usr/local/whisper/build-Linux:/home/lpt-10xe/riscv-arch-test/sail-riscv/build/c_emulator:/home/lpt-10xe/sail/bin:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin'
    }

    stages {
        stage('Preflight') {
            steps {
                sh 'ci/jenkins/weekly_vf2.sh preflight'
            }
        }

        stage('Generate + Sail + Package') {
            steps {
                sh 'ci/jenkins/weekly_vf2.sh prepare'
            }
        }

        stage('Spike Reference') {
            when { expression { return params.RUN_SPIKE } }
            steps {
                catchError(buildResult: 'UNSTABLE', stageResult: 'UNSTABLE') {
                    sh 'ci/jenkins/weekly_vf2.sh spike'
                }
            }
        }

        stage('Attach SD Card') {
            when { expression { return params.RUN_HARDWARE } }
            steps {
                input message: "Attach the dedicated VF2 SD card as ${params.SD_DEV}. The next stage overwrites its VF2 boot slot and ACT tail pack.",
                      ok: 'SD card is attached'
            }
        }

        stage('Flash VF2') {
            when { expression { return params.RUN_HARDWARE } }
            steps {
                sh 'ci/jenkins/weekly_vf2.sh flash'
            }
        }

        stage('Move SD Card to VF2') {
            when { expression { return params.RUN_HARDWARE } }
            steps {
                input message: 'Safely remove the SD card from the host, insert it into VF2, and confirm UART and smart-plug control are connected.',
                      ok: 'VF2 is ready'
            }
        }

        stage('VF2 Execute + Report') {
            when { expression { return params.RUN_HARDWARE } }
            steps {
                catchError(buildResult: 'UNSTABLE', stageResult: 'UNSTABLE') {
                    sh 'ci/jenkins/weekly_vf2.sh run'
                }
            }
        }
    }

    post {
        always {
            sh 'ci/jenkins/weekly_vf2.sh finalize || true'
            script {
                def descriptionFile = "logs/jenkins/weekly/jenkins_weekly_${env.BUILD_NUMBER}/build_description.txt"
                if (fileExists(descriptionFile)) {
                    currentBuild.description = readFile(descriptionFile).trim()
                }
            }
            archiveArtifacts artifacts: "logs/jenkins/weekly/jenkins_weekly_${env.BUILD_NUMBER}/**, logs/runs/jenkins_weekly_${env.BUILD_NUMBER}/summary.md, logs/runs/jenkins_weekly_${env.BUILD_NUMBER}/cases.csv, logs/runs/jenkins_weekly_${env.BUILD_NUMBER}/cases.json, logs/runs/jenkins_weekly_${env.BUILD_NUMBER}/collection.json, logs/runs/jenkins_weekly_${env.BUILD_NUMBER}/uart*.log, logs/runs/jenkins_weekly_${env.BUILD_NUMBER}/per_case/**, logs/reference-model-runs/jenkins_weekly_${env.BUILD_NUMBER}/**, external/riscv-arch-test/work-vf2-jenkins-all-priv/visionfive2-rv64gc/build/priv/**/*.sig.log, external/riscv-arch-test/work-vf2-jenkins-all-priv/visionfive2-rv64gc/build/priv/**/*.results",
                             allowEmptyArchive: true,
                             fingerprint: false
            publishHTML target: [
                allowMissing: true,
                alwaysLinkToLastBuild: false,
                keepAll: true,
                reportDir: "logs/jenkins/weekly/jenkins_weekly_${env.BUILD_NUMBER}/site",
                reportFiles: 'index.html',
                reportName: 'Result Summary',
                reportTitles: 'VF2 Privileged-Test Results'
            ]
        }
    }
}
