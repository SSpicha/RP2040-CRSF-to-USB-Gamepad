import React from 'react';

interface VisualizerProps {
  axisMap: number[];
  channels: number[];
}

export const GamepadVisualizer: React.FC<VisualizerProps> = ({ axisMap, channels }) => {
  const AXIS_NAMES = ["X", "Y", "Z", "RZ", "RX", "RY"];

  return (
    <div className="gamepad-viz">
      {AXIS_NAMES.map((name, i) => {
        const chIdx = axisMap[i];
        const val = channels[chIdx] ?? 992;
        const pct = ((val - 172) / (1811 - 172)) * 100;
        
        return (
          <div key={name} className="viz-axis">
            <div className="viz-label">{name} (CH{chIdx + 1})</div>
            <div className="viz-bar-bg">
              <div 
                className="viz-bar-fill" 
                style={{ width: `${pct}%`, backgroundColor: `hsl(${pct * 1.2}, 70%, 50%)` }} 
              />
            </div>
          </div>
        );
      })}
    </div>
  );
};
