// The swarm flight model, free of the DOM: point agents in a viewport. Each
// butterfly flies at its own cruise speed (drawn like the benchmark flight's),
// turns at random moments, bounces off the edges of its field (the whole
// screen, or a centred share of its width and height) and off another
// butterfly when their bodies touch, and otherwise flies straight through the
// others. Bouncing flight in a box spreads them evenly over the field.

export interface SwarmAgent {
    x: number;
    y: number;
    vx: number;
    vy: number;
    /** Cruise speed, in pixels per second. */
    speed: number;
    /** Displayed scale: nearer butterflies are larger. */
    scale: number;
    /** The direction it steers towards, in radians (0 along +x, y down). */
    course: number;
    /** Heading in degrees, 0 with the head up, smoothed from the velocity. */
    heading: number;
    /** Seconds until take-off, since take-off, until the next turn, and until a leaving one heads off. */
    delay: number;
    flown: number;
    turnIn: number;
    exitIn: number;
    /** Its first turn is the bounce just after take-off. */
    bounced: boolean;
    exiting: boolean;
    gone: boolean;
}

// Like the benchmark flight, a butterfly reaches its cruise speed in 0.2 s.
const TAKEOFF_SECONDS = 0.2;
// Releases spread over this time, so the take-offs are not in step.
const RELEASE_SECONDS = 0.35;
// How fast a butterfly turns towards a new course, in radians per second.
const TURN_RATE = 9;
// Seconds between random turns: a minimum plus an exponential wait.
const TURN_MIN = 0.25;
const TURN_MEAN = 0.7;
// Far from the centre, a random turn heads back towards it this often.
const CENTRE_BIAS = 0.5;
// The body's radius as a share of the butterfly's size.
const BODY = 0.07;
// When two bodies touch, they bounce apart this often; otherwise they fly
// through each other. Each contact is decided once.
const BOUNCE_CHANCE = 0.6;
// The longest step, small enough that fast bodies cannot pass through.
const STEP_SECONDS = 1 / 240;

const wrap = (angle: number) => ((angle % (2 * Math.PI)) + 3 * Math.PI) % (2 * Math.PI) - Math.PI;
const jitter = (spread: number) => (Math.random() * 2 - 1) * spread;

export class Swarm {
    readonly agents: SwarmAgent[];
    /** Where the bodies fly, inside the screen. */
    readonly field: { left: number; top: number; right: number; bottom: number };
    /** Pairs whose bodies touch, by index i * count + j. */
    readonly contacts: Uint8Array;
    time = 0;
    leaving = false;

    constructor(readonly width: number, readonly height: number, readonly size: number,
                count: number, share: number, speed: () => number) {
        const margin = (1 - share) / 2;
        this.field = { left: width * margin, top: height * margin, right: width * (1 - margin), bottom: height * (1 - margin) };
        this.agents = Array.from({ length: count }, () => ({ x: 0, y: 0, vx: 0, vy: 0, speed: speed(),
            scale: 0.8 + Math.random() * 0.3, course: 0, heading: 0, delay: 0, flown: 0, turnIn: 0, exitIn: 0,
            bounced: false, exiting: false, gone: false }));
        this.contacts = new Uint8Array(count * count);
    }

    /**
     * Releases the swarm from an area (the butterfly on the stage): each one
     * takes off from its own point within it, at its own moment, outwards and
     * at random, and soon bounces off in a random direction.
     */
    release(area: { x: number; y: number; width: number; height: number }) {
        for (const agent of this.agents) {
            const angle = Math.random() * Math.PI * 2, reach = Math.sqrt(Math.random()) * 0.4;
            agent.x = area.x + area.width * (0.5 + Math.cos(angle) * reach);
            agent.y = area.y + area.height * (0.5 + Math.sin(angle) * reach);
            agent.course = angle + jitter(1.2);
            agent.vx = Math.cos(agent.course) * agent.speed * 0.05;
            agent.vy = Math.sin(agent.course) * agent.speed * 0.05;
            agent.heading = agent.course * 180 / Math.PI + 90;
            agent.delay = Math.random() * RELEASE_SECONDS;
            agent.turnIn = 0.15 + Math.random() * 0.35;
        }
    }

    /** Places the agents at random over the field, at rest, for reduced motion. */
    settle() {
        const { left, top, right, bottom } = this.field;
        for (const agent of this.agents) {
            agent.x = left + Math.random() * (right - left);
            agent.y = top + Math.random() * (bottom - top);
            agent.heading = jitter(180);
            agent.delay = 0;
            agent.flown = 1;
        }
    }

    /** Every butterfly heads off the screen through a nearby side, at its own moment. */
    leave() {
        this.leaving = true;
        for (const agent of this.agents) {
            if (agent.delay > 0) agent.gone = true;
            agent.exitIn = Math.random() * 0.5;
        }
    }

    get done() { return this.agents.every(agent => agent.gone); }

    step(seconds: number) {
        const steps = Math.max(1, Math.ceil(seconds / STEP_SECONDS));
        for (let i = 0; i < steps; ++i) this.advance(seconds / steps);
    }

    /** Turns the velocity at once, keeping its speed, and sets the course. */
    point(agent: SwarmAgent, angle: number) {
        const speed = Math.hypot(agent.vx, agent.vy);
        agent.vx = Math.cos(angle) * speed;
        agent.vy = Math.sin(angle) * speed;
        agent.course = angle;
    }

    /** The bounce just after take-off, then random turns, often back towards the centre when far from it. */
    turn(agent: SwarmAgent) {
        agent.turnIn = TURN_MIN - Math.log(1 - Math.random()) * TURN_MEAN;
        if (!agent.bounced) {
            agent.bounced = true;
            return this.point(agent, Math.atan2(agent.vy, agent.vx) + Math.sign(jitter(1)) * (1 + Math.random() * 2));
        }
        const { left, top, right, bottom } = this.field;
        const toX = (left + right) / 2 - agent.x, toY = (top + bottom) / 2 - agent.y;
        if (Math.hypot(2 * toX / (right - left), 2 * toY / (bottom - top)) > 0.55 && Math.random() < CENTRE_BIAS)
            agent.course = Math.atan2(toY, toX) + jitter(1);
        else agent.course += Math.sign(jitter(1)) * (0.5 + Math.random() * 2.1);
    }

    advance(dt: number) {
        this.time += dt;
        const { left, top, right, bottom } = this.field;
        // On the whole screen the wings may cross its edge and the body
        // bounces a little further in; a smaller field bounces at its edge.
        const inset = left > 0 ? 0 : this.size * 0.3;
        for (const agent of this.agents) {
            if (agent.gone) continue;
            if (agent.delay > 0) { agent.delay -= dt; continue; }
            agent.flown += dt;
            if (this.leaving && !agent.exiting && (agent.exitIn -= dt) <= 0) {
                // Out through the nearest side of the screen, at a random angle.
                agent.exiting = true;
                const sides = [[agent.x, Math.PI], [this.width - agent.x, 0], [agent.y, -Math.PI / 2], [this.height - agent.y, Math.PI / 2]];
                agent.course = sides.sort((a, b) => a[0] - b[0])[0][1] + jitter(0.7);
            } else if (!agent.exiting) {
                if (agent.bounced && (agent.x < left || agent.x > right || agent.y < top || agent.y > bottom)) {
                    // Released off the field (the stage below a phone's
                    // screen): straight back towards its middle.
                    agent.course = Math.atan2((top + bottom) / 2 - agent.y, (left + right) / 2 - agent.x);
                } else if ((agent.turnIn -= dt) <= 0) this.turn(agent);
            }
            // Steer towards the course at most at the turn rate, and reach
            // cruise speed as the benchmark take-off does.
            const current = Math.atan2(agent.vy, agent.vx);
            const direction = current + Math.max(-TURN_RATE * dt, Math.min(TURN_RATE * dt, wrap(agent.course - current)));
            const target = agent.exiting ? agent.speed * 1.15 : agent.speed;
            const velocity = Math.min(target, Math.hypot(agent.vx, agent.vy) + target / TAKEOFF_SECONDS * dt);
            agent.vx = Math.cos(direction) * velocity;
            agent.vy = Math.sin(direction) * velocity;
            agent.x += agent.vx * dt;
            agent.y += agent.vy * dt;
            if (agent.exiting) {
                const out = this.size;
                agent.gone = agent.x < -out || agent.y < -out || agent.x > this.width + out || agent.y > this.height + out;
            } else {
                // Bounce off the field's edges, a little off the mirror angle.
                if ((agent.x < left + inset && agent.vx < 0) || (agent.x > right - inset && agent.vx > 0))
                    this.point(agent, Math.atan2(agent.vy, -agent.vx) + jitter(0.35));
                if ((agent.y < top + inset && agent.vy < 0) || (agent.y > bottom - inset && agent.vy > 0))
                    this.point(agent, Math.atan2(-agent.vy, agent.vx) + jitter(0.35));
            }
            // The head turns quickly, but not in a single frame, after a bounce.
            const turn = ((Math.atan2(agent.vx, -agent.vy) * 180 / Math.PI - agent.heading) % 360 + 540) % 360 - 180;
            agent.heading += turn * Math.min(1, dt * 18);
        }
        // Bodies that touch bounce apart like equal balls, each keeping its
        // speed, or fly through each other.
        const { agents } = this, count = agents.length;
        for (let i = 0; i < count; ++i) {
            const a = agents[i];
            if (a.gone || a.delay > 0) continue;
            for (let j = i + 1; j < count; ++j) {
                const b = agents[j];
                if (b.gone || b.delay > 0) continue;
                const dx = a.x - b.x, dy = a.y - b.y, reach = this.size * BODY * (a.scale + b.scale);
                const pair = i * count + j;
                if (dx * dx + dy * dy >= reach * reach) { this.contacts[pair] = 0; continue; }
                if (this.contacts[pair]) continue;
                this.contacts[pair] = 1;
                const distance = Math.hypot(dx, dy) || 1;
                const nx = dx / distance, ny = dy / distance;
                const closing = (a.vx - b.vx) * nx + (a.vy - b.vy) * ny;
                if (closing >= 0 || Math.random() >= BOUNCE_CHANCE) continue;
                // They exchange their velocities along the line between them.
                for (const [agent, sign] of [[a, -1], [b, 1]] as const)
                    this.point(agent, Math.atan2(agent.vy + sign * closing * ny, agent.vx + sign * closing * nx) + jitter(0.25));
            }
        }
    }
}
