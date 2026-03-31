# Rock Paper Scissors Battle Royale (TCP Select Server)

## Overview

This project is a real-time multiplayer TCP client-server game implementing a Rock–Paper–Scissors battle royale simulation.

The server is authoritative and is responsible for all game state simulation, including movement, collision resolution, and win conditions.

Clients are purely rendering and input agents.

---

## Architecture

### Server Model

- Single-threaded event loop
- Uses `select()` for multiplexed I/O
- Fully authoritative simulation
- Fixed tick rate simulation (default 60 Hz)

### Client Model

- Connects via TCP
- Sends initial spawn request
- Receives full world state updates
- Renders state locally (no simulation logic)

---

## Constraints

- No threading or fork-based concurrency in server
- Server must never block on a single client
- All client communication handled via `select()`
- TCP only (no UDP)

---

## Game World

- 2D continuous plane
- Fixed size: 1280 × 960
- No wrap-around boundaries
- Players are constrained within bounds

---

## Player Limit

- Maximum recommended players: 16–32

### Rationale:

- O(n²) simulation cost for target selection and collision
- O(n²) worst-case network broadcast behavior
- FD_SET and select() scalability constraints
- Educational scope limitation

---

## Game Rules

Each player is assigned one type:

- Rock
- Paper
- Scissors

### Interaction Rules

- Rock defeats Scissors
- Scissors defeats Paper
- Paper defeats Rock

### Targeting Rule

Each player continuously targets:

- The closest valid prey (based on Euclidean distance)
- If no prey exists, the player remains stationary

---

## Movement

- Constant speed for all players
- Movement direction is normalized vector toward target
- Server computes all movement
- No client-side prediction or simulation

---

## Collision

- If two players of opposing types collide:
  - Winner survives
  - Loser is marked dead
- Players of same type may overlap freely

---

## Game Phases

### Lobby Phase

- Duration: 30 seconds
- Clients may join and send spawn requests:

### Simulation Phase

- Starts after lobby ends
- Runs indefinitely until termination condition is met

### Termination Condition

- Game ends when only one type remains OR one player remains alive

---

## Networking Protocol

### Transport

- TCP

---

## Binary Message Format

All messages are fixed-format binary packets.

### Client → Server

#### SPAWN message
