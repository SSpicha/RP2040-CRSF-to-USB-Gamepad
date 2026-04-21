import React from 'react';
import type { AxisMapping } from "./types";

interface SvgProps {
  channels: number[];
  axisMap: AxisMapping[];
  buttons: boolean[];
}
export const GamepadSvg: React.FC<SvgProps> = ({ channels, axisMap, buttons }) => {
  const getVal = (idx: number) => {
    const mapping = axisMap[idx];
    if (!mapping) return 0;
    const raw = channels[mapping.ch] ?? 992;
    const min = mapping.min ?? 172;
    const max = mapping.max ?? 1811;
    const center = (min + max) / 2;

    let target;
    if (Math.abs(raw - center) < 4) {
      target = 0;
    } else if (raw >= center) {
      target = ((raw - center) * 32767) / (max - center);
    } else {
      target = ((raw - center) * 32767) / (center - min);
    }

    // Normalize to -1..1
    const val = target / 32767;
    return mapping.invert ? -val : val;
  };

  const getTriggerVal = (idx: number) => {
    const mapping = axisMap[idx];
    if (!mapping) return 0;
    const raw = channels[mapping.ch] ?? 172;
    const min = mapping.min ?? 172;
    const max = mapping.max ?? 1811;
    const val = (raw - min) / (max - min);
    return Math.max(0, Math.min(1, val));
  };


  const lx = getVal(0); 
  const ly = getVal(1); 
  const rx = getVal(2); 
  const ry = getVal(3);
  const lt = getTriggerVal(4);
  const rt = getTriggerVal(5);

  // New Wide Layout Coordinates (ViewBox 300x120)
  const btnPositions = [
    // 1-4 Action Buttons (Far Right)
    { x: 260, y: 75, label: 'B1' }, // Bottom (A)
    { x: 275, y: 60, label: 'B2' }, // Right (B)
    { x: 245, y: 60, label: 'B3' }, // Left (X)
    { x: 260, y: 45, label: 'B4' }, // Top (Y)
    
    // 5-8 D-Pad (Far Left)
    { x: 40, y: 75, label: 'B5' },  // Down
    { x: 55, y: 60, label: 'B6' },  // Right
    { x: 25, y: 60, label: 'B7' },  // Left
    { x: 40, y: 45, label: 'B8' },  // Up

    // 9-10 Select / Start (Near Center)
    { x: 130, y: 45, label: 'B9' },
    { x: 170, y: 45, label: 'B10' },

    // 11-12 Bumpers (Top)
    { x: 70, y: 22, label: 'B11' },
    { x: 230, y: 22, label: 'B12' },

    // 13-14 L3 / R3 (Below Sticks)
    { x: 95, y: 105, label: 'B13' },
    { x: 205, y: 105, label: 'B14' },

    // 15-16 Extras (Center Column)
    { x: 150, y: 35, label: 'B15' },
    { x: 150, y: 55, label: 'B16' }
  ];

  const L_STICK_X = 95;
  const L_STICK_Y = 70;
  const R_STICK_X = 205;
  const R_STICK_Y = 70;
  const STICK_MAX_MOVE = 18;

  return (
    <div style={{ display: 'flex', flexDirection: 'column', alignItems: 'center', width: '100%' }}>
      <svg viewBox="0 0 300 120" className="gamepad-svg" style={{ width: '100%', maxWidth: '800px', height: 'auto' }}>
        {/* Gamepad Body Outline - Wider and more ergonomic */}
        <path d="M70,20 L230,20 Q280,20 295,60 L285,100 Q270,115 230,110 L70,110 Q30,115 5,100 L5,60 Q20,20 70,20" 
              fill="#1a1f26" stroke="#3a4454" strokeWidth="2.5" />
        
        {/* Triggers Visualization (LT / RT) */}
        <rect x="60" y="5" width="40" height="12" rx="3" fill="#0f131a" stroke="#2f3848" />
        <rect x="60" y="5" width={40 * lt} height="12" rx="3" fill="#2e6ae6" />
        <text x="80" y="14" textAnchor="middle" fill="#fff" fontSize="8" style={{ fontWeight: 'bold' }} pointerEvents="none">LT</text>

        <rect x="200" y="5" width="40" height="12" rx="3" fill="#0f131a" stroke="#2f3848" />
        <rect x="200" y="5" width={40 * rt} height="12" rx="3" fill="#ff9f1c" />
        <text x="220" y="14" textAnchor="middle" fill="#fff" fontSize="8" style={{ fontWeight: 'bold' }} pointerEvents="none">RT</text>

        {/* Sticks Zones */}
        <circle cx={L_STICK_X} cy={L_STICK_Y} r="25" fill="#0f131a" stroke="#2f3848" strokeWidth="1.5" />
        <circle cx={R_STICK_X} cy={R_STICK_Y} r="25" fill="#0f131a" stroke="#2f3848" strokeWidth="1.5" />

        {/* Stick Caps */}
        <circle cx={L_STICK_X + lx * STICK_MAX_MOVE} cy={L_STICK_Y - ly * STICK_MAX_MOVE} r="10" fill="#2e6ae6" stroke="#fff" strokeWidth="0.5" />
        <circle cx={R_STICK_X + rx * STICK_MAX_MOVE} cy={R_STICK_Y - ry * STICK_MAX_MOVE} r="10" fill="#ff9f1c" stroke="#fff" strokeWidth="0.5" />

        {/* Buttons Rendering */}
        {btnPositions.map((pos, i) => (
          <g key={i}>
            <circle cx={pos.x} cy={pos.y} r="6" 
                    fill={buttons[i] ? "#00f5d4" : "#2f3848"} 
                    stroke={buttons[i] ? "#fff" : "#3a4454"} strokeWidth="1" />
            <text x={pos.x} y={pos.y + 2} textAnchor="middle" fill={buttons[i] ? "#000" : "#aaa"} 
                  fontSize="5" style={{ fontWeight: 'bold', pointerEvents: 'none' }}>
              {pos.label}
            </text>
          </g>
        ))}

        {/* Labels below sticks */}
        <text x={L_STICK_X} y={115} textAnchor="middle" fill="#aaa" fontSize="7" style={{ opacity: 0.7 }}>LS (Yaw/Thr)</text>
        <text x={R_STICK_X} y={115} textAnchor="middle" fill="#aaa" fontSize="7" style={{ opacity: 0.7 }}>RS (Roll/Pitch)</text>
      </svg>
      <div style={{ marginTop: '0.8rem', display: 'flex', gap: '2rem', color: '#aaa', fontSize: '0.9rem', fontWeight: 'bold' }}>
        <span style={{ color: '#2e6ae6' }}>LT: {Math.round(lt*100)}%</span>
        <span style={{ color: '#ff9f1c' }}>RT: {Math.round(rt*100)}%</span>
      </div>
    </div>
  );
};
