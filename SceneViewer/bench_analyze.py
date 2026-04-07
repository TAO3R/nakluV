import pandas as pd
import matplotlib.pyplot as plt
import os

# --- 1. Path Setup ---
script_dir = os.path.dirname(os.path.abspath(__file__))
csv_path = os.path.join(script_dir, '../../A1/report/benchmarks/gpu_bottleneck/gpu_7680_4320.csv')
filename_only = os.path.basename(csv_path)

try:
    df = pd.read_csv(csv_path)
except FileNotFoundError:
    df = pd.read_csv('Articulation_frustum_1920_1080_Arm-Camera.csv') # Fallback
    filename_only = "Local_File.csv"

# --- 2. Calculate Stats ---
mean_val = df['render_ms'].mean()
median_val = df['render_ms'].median()
d_min, d_max = df['render_ms'].min(), df['render_ms'].max()
d_range = d_max - d_min

# --- 3. Robust Scaling Logic ---
# If the range is extremely small (e.g. < 0.1ms difference), 
# we force a window around the mean so the chart doesn't look broken.
if d_range < 0.1:
    y_bottom = mean_val - 0.5
    y_top = mean_val + 0.5
else:
    # Use 20% padding to give the curve "breathing room" but keep it centered
    y_bottom = d_min - (d_range * 0.2)
    y_top = d_max + (d_range * 0.2)

# --- 4. Generate the Chart ---
plt.figure(figsize=(14, 8))

# Draw the 60 FPS Target ONLY if it's within our zoomed view
target_60fps = 16.666
if y_bottom < target_60fps < y_top:
    plt.axhline(y=target_60fps, color='gray', linestyle='-', alpha=0.3, label='60 FPS Target')
    plt.text(df['frame'].min(), target_60fps + (d_range*0.02), ' 60 FPS', color='gray', alpha=0.5)

# Plot Data
plt.plot(df['frame'], df['render_ms'], color='#1f77b4', label='Frame Time', linewidth=1.5, zorder=3)

# Mean/Median Lines (Dashed)
plt.axhline(y=mean_val, color='red', linestyle='--', linewidth=2, label=f'Mean: {mean_val:.3f}ms', zorder=4)
plt.axhline(y=median_val, color='green', linestyle=':', linewidth=2, label=f'Median: {median_val:.3f}ms', zorder=4)

# --- 5. Apply the Zoom ---
plt.ylim(y_bottom, y_top)

# --- 6. Large Stats Box ---
# Placing it in the bottom-right to ensure it doesn't block the "top" where spikes happen
stats_text = f"MEAN: {mean_val:.3f} ms\nMEDIAN: {median_val:.3f} ms"
plt.text(0.98, 0.05, stats_text, transform=plt.gca().transAxes, 
         fontsize=22, fontweight='bold', ha='right', va='bottom',
         bbox=dict(boxstyle='round,pad=0.5', facecolor='yellow', alpha=0.4))

# --- 7. Final Formatting ---
plt.title(f"Performance Detail: {filename_only}", fontsize=18, fontweight='bold', pad=20)
plt.xlabel('Frame Index', fontsize=14)
plt.ylabel('Render Time (ms)', fontsize=14)
plt.grid(True, which='both', linestyle='--', alpha=0.3)
plt.legend(loc='upper left', frameon=True, shadow=True)

plt.tight_layout()
plt.savefig('zoomed_performance_plot.png', dpi=300)
plt.show()