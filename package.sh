#!/usr/bin/env bash

flatpak-builder --repo=flatpak-build/repo flatpak-build org.bloompa.EqualizerPWF.yml --user --install-deps-from=flathub --force-clean
flatpak build-bundle flatpak-build/repo flatpak-build/org.bloompa.EqualizerPWF.flatpak org.bloompa.EqualizerPWF

