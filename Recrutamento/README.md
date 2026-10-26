# Formula Student | Electronics & Software

<p align="center">
  <strong>Recruitment Guide & Embedded Systems Onboarding</strong><br>
  Git/GitHub workflow · Raspberry Pi Pico 2 W · TSAL & Brake Light simulations
</p>

---

## Overview

This repository supports the onboarding of new members joining the **Electronics & Software Department**. It introduces the team's collaborative development workflow and provides a practical embedded-systems exercise using the Raspberry Pi Pico 2 W.

The accompanying recruitment guide is a work in progress and covers the topics outlined below.

## What's Inside

- **Git & GitHub** — Repositories, branches, commits, Pull Requests, and collaboration within a GitHub Organization.
- **Pico 2 W setup** — Getting started with Arduino IDE or VS Code + PlatformIO.
- **TSAL simulation** — A simplified Tractive System Active Light exercise using LEDs, buttons, and state-based logic.
- **Brake Light simulation** — Digital and analog input handling, thresholds, hysteresis, and fail-safe behaviour.
- **Engineering practices** — Wiring checks, debugging, code quality, and Pull Request review.

## Hardware Reference

The practical exercise uses the **Raspberry Pi Pico 2 W**.

<p align="center">
  <img src="assets/RB_Pico_2W_PinOut.png" alt="Raspberry Pi Pico 2 W pinout" width="760">
</p>

Check the pinout and the guide before wiring the circuit. Confirm the GPIO assignments used by your implementation before powering the board.

## Development Tools

| Tool | Purpose | Link |
|---|---|---|
| Git | Version control | [git-scm.com](https://git-scm.com/) |
| GitHub Desktop | Visual Git workflow | [desktop.github.com](https://desktop.github.com/) |
| Arduino IDE | Board setup and firmware development | [arduino.cc](https://www.arduino.cc/en/software) |
| VS Code + PlatformIO | Alternative embedded development workflow | [platformio.org](https://platformio.org/) |

## Recommended Contribution Workflow

1. Clone the team repository and update your local `main` branch.
2. Create a dedicated branch for your task.
3. Implement the change and test it on the target setup.
4. Commit focused changes with clear, descriptive messages.
5. Push your branch and open a Pull Request.
6. Respond to review feedback and merge only after approval.

> **Team rule:** Do not commit directly to `main`. Contributions should be submitted through a Pull Request and reviewed before merging.

## Safety

These exercises are **educational simulations**, not competition-ready safety systems. Real TSAL and brake-light implementations must meet the applicable Formula Student regulations and undergo the required engineering validation.

- Verify the circuit before applying power.
- Pico 2 W GPIOs operate at **3.3 V** and are not 5 V tolerant.
- Use a suitable series resistor with every external LED.
- Do not connect GPIOs directly to high-power loads.
- Work on high-voltage systems only with the required training and supervision.

## Recruitment Guide

The full guide is intended to be provided as a PDF alongside this README. It is currently a work in progress.

---

<p align="center">
  <strong>Electronics & Software Department</strong><br>
  <em>Learn · Build · Test · Collaborate</em>
</p>
