import tkinter as tk
from tkinter import font
import cv2
from PIL import Image, ImageTk
import serial
import threading
import time
import os
import random
import math

SERIAL_PORT = 'COM11' # Make sure this matches your Hub's COM port!
BAUD_RATE = 115200

# Dictionary of your videos
VIDEOS = {'G': "gangnamfinal.mp4", 'P': "partyfinal.mp4"}

# Define exactly when (in seconds) the score screen should pop up for each video.
SCORE_TIMES = {'G': 98.0, 'P': 85.0} 

MAX_SCORE = 5300  # Updated Max Score

# ==========================================
# --- NEW VARIABLES TO MESS WITH ---
# ==========================================

# Start the video at a specific point in time (in milliseconds)
# e.g., 10000 = start 10 seconds into the video.
VIDEO_START_OFFSET_MS = 0 

# ==========================================

class JustDanceGUI:
    def __init__(self, root):
        self.root = root
        self.root.title("Just Dance Sync Hub")
        self.root.geometry("1280x720")
        self.root.configure(bg='black')

        self.p1_total_score = 0
        self.p2_total_score = 0
        self.p1_timer = None
        self.p2_timer = None
        self.start_time = None
  
        self.current_frame = 0
        
        self.current_end_time = 9999.0
        self.score_shown = False
        
        # Calibration state flags (Independent now)
        self.p1_calibrating = False
        self.p2_calibrating = False
        self.flash_color_state = False

        # --- Layout ---
  
        self.video_frame = tk.Frame(self.root, bg='black')
        self.video_frame.place(relx=0, rely=0, relwidth=1.0, relheight=1.0)
        
        self.left_sidebar_frame = tk.Frame(self.root, bg='#1a1a1a')
        self.right_sidebar_frame = tk.Frame(self.root, bg='#1a1a1a')

        # --- Video Canvas ---
        self.video_label = tk.Label(self.video_frame, bg='black')
        self.video_label.pack(expand=True, fill="both")
        self.cap = None
        
        self.video_running = False

        # --- End Screen Overlay ---
        self.end_frame = tk.Frame(self.video_frame, bg='#161625', bd=8, relief=tk.RIDGE)
        
        self.end_title = tk.Label(self.end_frame, text="🎵 DANCE COMPLETE! 🎵", font=("Century Gothic", 38, "bold"), fg="#ffffff", bg="#161625")
        self.end_title.pack(pady=(40, 30))

        self.end_p1_score = tk.Label(self.end_frame, text="", font=("Impact", 34), fg="#00FFFF", bg="#161625")
        self.end_p1_score.pack(pady=10)

        self.end_p2_score = tk.Label(self.end_frame, text="", font=("Impact", 34), fg="#FF4500", bg="#161625")
        self.end_p2_score.pack(pady=10)

        self.end_winner = tk.Label(self.end_frame, text="", font=("Century Gothic", 36, "bold"), fg="#FFD700", bg="#161625")
        self.end_winner.pack(pady=(40, 40))

        # --- Sidebar Canvases ---
 
        # Fixed size centered in the relative frame. 
        self.left_canvas = tk.Canvas(self.left_sidebar_frame, width=192, height=720, bg='#1a1a1a', highlightthickness=0)
        self.left_canvas.place(relx=0.5, rely=0.5, anchor=tk.CENTER)
        
        self.right_canvas = tk.Canvas(self.right_sidebar_frame, width=192, height=720, bg='#1a1a1a', highlightthickness=0)
        self.right_canvas.place(relx=0.5, rely=0.5, anchor=tk.CENTER)

        # --- Fonts ---
        self.title_font = font.Font(family="Century Gothic", size=26, weight="bold")
     
        # Fonts for the Feedback Words Pop Animation 
        self.feedback_font_large = font.Font(family="Impact", size=32, slant="italic")
        self.feedback_font_med = font.Font(family="Impact", size=26, slant="italic")
        self.feedback_font_small = font.Font(family="Impact", size=20, slant="italic") 
        
        # Fonts for the Number Score Pop Animation
        self.score_font_large = font.Font(family="Impact", size=32)
        self.score_font_med = font.Font(family="Impact", size=28)
        self.score_font_small = font.Font(family="Impact", size=24)

        self.star_font = font.Font(family="Segoe UI", size=22) 
        
        # --- Dimensions ---
        self.bar_w = 70 
        self.p_x = 96   
        self.bar_bottom = 680
        self.bar_top = 180
        self.bar_h = self.bar_bottom - self.bar_top

        # --- Titles ---
        self.left_canvas.create_text(self.p_x, 20, text="PLAYER 1", font=self.title_font, fill="#00FFFF")
        self.right_canvas.create_text(self.p_x, 20, text="PLAYER 2", font=self.title_font, fill="#FF4500")

        # --- Feedback Words & Scores ---
        # Player 1 (Pow polygon is drawn first so it stays behind text)
        self.p1_pow_bg = self.left_canvas.create_polygon([0, 0, 0, 0, 0, 0], fill="", outline="", width=3)
        self.p1_feedback_shadow = self.left_canvas.create_text(self.p_x + 3, 90 + 3, text="", font=self.feedback_font_small, fill="#000000")
        self.p1_feedback = self.left_canvas.create_text(self.p_x, 90, text="", font=self.feedback_font_small, fill="white")
        
        self.p1_num_score_shadow = self.left_canvas.create_text(self.p_x + 3, 140 + 3, text="", font=self.score_font_small, fill="#000000")
        self.p1_num_score = self.left_canvas.create_text(self.p_x, 140, text="", font=self.score_font_small, fill="white")

        # Player 2
        self.p2_pow_bg = self.right_canvas.create_polygon([0, 0, 0, 0, 0, 0], fill="", outline="", width=3)
        self.p2_feedback_shadow = self.right_canvas.create_text(self.p_x + 3, 90 + 3, text="", font=self.feedback_font_small, fill="#000000")
        self.p2_feedback = self.right_canvas.create_text(self.p_x, 90, text="", font=self.feedback_font_small, fill="white")
        
        self.p2_num_score_shadow = self.right_canvas.create_text(self.p_x + 3, 140 + 3, text="", font=self.score_font_small, fill="#000000")
        self.p2_num_score = self.right_canvas.create_text(self.p_x, 140, text="", font=self.score_font_small, fill="white")

        # --- Bars ---
        self._draw_static_gradient(self.left_canvas, self.p_x - self.bar_w/2, self.bar_top, self.bar_bottom, self.bar_w, "#FF00FF", "#00FFFF")
        self._draw_static_gradient(self.right_canvas, self.p_x - self.bar_w/2, self.bar_top, self.bar_bottom, self.bar_w, "#B22222", "#FFFF00")

        self.p1_mask = self.left_canvas.create_rectangle(self.p_x - self.bar_w/2, self.bar_top, self.p_x + self.bar_w/2, self.bar_bottom, fill="#333333", outline="")
        self.p2_mask = self.right_canvas.create_rectangle(self.p_x - self.bar_w/2, self.bar_top, self.p_x + self.bar_w/2, self.bar_bottom, fill="#333333", outline="")

        # --- Glowing Bar Caps (Leading Edge) ---
        self.p1_cap = self.left_canvas.create_rectangle(self.p_x - self.bar_w/2 - 4, self.bar_bottom - 2, self.p_x + self.bar_w/2 + 4, self.bar_bottom + 2, fill="white", outline="")
        self.p2_cap = self.right_canvas.create_rectangle(self.p_x - self.bar_w/2 - 4, self.bar_bottom - 2, self.p_x + self.bar_w/2 + 4, self.bar_bottom + 2, fill="white", outline="")

        # --- Stars ---
        self.p1_stars, self.p1_glows = [], []
        self.p2_stars, self.p2_glows = [], []
        
        p1_star_x = self.p_x + self.bar_w/2 + 25
        p2_star_x = self.p_x - self.bar_w/2 - 25

        for i in range(1, 6): 
     
            y_pos = self.bar_bottom - (self.bar_h * (i / 5.0)) + 15
            
            # P1 Stars
            g1 = self.left_canvas.create_text(p1_star_x, y_pos + 2, text="★", font=self.star_font, fill="#1a1a1a") 
            s1 = self.left_canvas.create_text(p1_star_x, y_pos, text="★", font=self.star_font, fill="#444444")
            self.p1_glows.append(g1)
      
            self.p1_stars.append(s1)
            
            # P2 Stars
            g2 = self.right_canvas.create_text(p2_star_x, y_pos + 2, text="★", font=self.star_font, fill="#1a1a1a")
            s2 = self.right_canvas.create_text(p2_star_x, y_pos, text="★", font=self.star_font, fill="#444444")
            self.p2_glows.append(g2)
            self.p2_stars.append(s2)

      
        # --- Status Texts ---
        self.status_text = self.left_canvas.create_text(self.p_x, 690, text="WAITING", font=("Century Gothic", 20, "bold"), fill="yellow")
        self.p2_status_text = self.right_canvas.create_text(self.p_x, 690, text="WAITING", font=("Century Gothic", 20, "bold"), fill="yellow")
        
        self.game_active = False 

        # --- Serial Setup ---
        try:
            self.ser = serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=0.1)
     
            self.ser.reset_input_buffer()
        except Exception as e:
            print(f"Serial Error: {e}")
            self.ser = None

        if self.ser:
            self.running = True
            self.serial_thread = threading.Thread(target=self.read_serial, daemon=True)
            self.serial_thread.start()

    
    def _draw_static_gradient(self, canvas, x, y_top, y_bottom, width, color_top, color_bottom):
        r1, g1, b1 = self.root.winfo_rgb(color_top)
        r2, g2, b2 = self.root.winfo_rgb(color_bottom)
        height = y_bottom - y_top
        for i in range(height):
            ratio = i / height
            r = int(r1 + (r2 - r1) * ratio)
         
            g = int(g1 + (g2 - g1) * ratio)
            b = int(b1 + (b2 - b1) * ratio)
            hex_color = f"#{r>>8:02x}{g>>8:02x}{b>>8:02x}"
            canvas.create_line(x, y_top + i, x + width, y_top + i, fill=hex_color)

    def _get_pow_coords(self, cx, cy, scale_factor, is_bad=False):
        """Generates dynamic starburst coordinates for the comic book effect."""
        
        coords = []
        num_points = 9 if is_bad else 14 
        outer_r = 85 * scale_factor
        inner_r = 45 * scale_factor
        
        # Make the bad feedback sharper and smaller
        if is_bad:
            outer_r *= 0.85
            inner_r *= 0.6
            
        angle_step = math.pi / num_points
        for i in range(2 * num_points):
            r = outer_r if i % 2 == 0 else inner_r
            # Add a jagged, comic-book irregularity to the outer points
            if i % 2 == 0:
  
                r += (i % 3) * 8 * scale_factor
            
            angle = i * angle_step
            coords.append(cx + r * math.cos(angle))
            coords.append(cy + r * math.sin(angle))
        return coords

    def load_and_preroll(self, key):
   
        if key not in VIDEOS: return
        self.game_active = False 
        self.video_running = False
        self.p1_calibrating = False 
        self.p2_calibrating = False
        self.p1_total_score = 0
        self.p2_total_score = 0
        self.score_shown = False
        
        self.current_end_time = SCORE_TIMES.get(key, 9999.0)
        
        self.end_frame.place_forget()
        self.left_sidebar_frame.place_forget()
        self.right_sidebar_frame.place_forget()
        self.video_frame.place(relx=0, relwidth=1.0)
        
        # Reset Masks and Caps
        self.left_canvas.coords(self.p1_mask, self.p_x - self.bar_w/2, self.bar_top, self.p_x + self.bar_w/2, self.bar_bottom)
        self.right_canvas.coords(self.p2_mask, self.p_x - self.bar_w/2, self.bar_top, self.p_x + self.bar_w/2, self.bar_bottom)
      
        self.left_canvas.coords(self.p1_cap, self.p_x - self.bar_w/2 - 4, self.bar_bottom - 2, self.p_x + self.bar_w/2 + 4, self.bar_bottom + 2)
        self.right_canvas.coords(self.p2_cap, self.p_x - self.bar_w/2 - 4, self.bar_bottom - 2, self.p_x + self.bar_w/2 + 4, self.bar_bottom + 2)

        self._clear_feedback(1)
        self._clear_feedback(2)

        for i in range(5):
            self.left_canvas.itemconfig(self.p1_stars[i], fill="#444444")
            self.left_canvas.itemconfig(self.p1_glows[i], fill="#1a1a1a") 
    
            self.right_canvas.itemconfig(self.p2_stars[i], fill="#444444")
            self.right_canvas.itemconfig(self.p2_glows[i], fill="#1a1a1a")

        if self.cap is not None:
            self.cap.release()
            
        video_path = os.path.abspath(VIDEOS[key])
        self.cap = cv2.VideoCapture(video_path)
        
        self.fps = self.cap.get(cv2.CAP_PROP_FPS)
    
        if self.fps <= 0: self.fps = 30 
        
        # --- JUMP TO THE SPECIFIED MILLISECOND OFFSET ---
        start_frame = int((VIDEO_START_OFFSET_MS / 1000.0) * self.fps)
        self.cap.set(cv2.CAP_PROP_POS_FRAMES, start_frame)
        self.current_frame = start_frame
        
        ret, frame = self.cap.read()
        if ret:
    
            self.display_cv2_frame(frame)
            self.current_frame += 1
            
        # Update BOTH status texts
        self.left_canvas.itemconfig(self.status_text, text="SYNC LOCKED", fill="#00FF00")
        self.right_canvas.itemconfig(self.p2_status_text, text="SYNC LOCKED", fill="#00FF00")
        
        if self.ser:
            self.ser.write(b'R') 
  
            self.ser.flush()

    def display_cv2_frame(self, frame):
        frame_rgb = cv2.cvtColor(frame, cv2.COLOR_BGR2RGB)
        target_w = self.video_frame.winfo_width()
        if target_w < 100: target_w = 1280 
        
        h, w, _ = frame_rgb.shape
        scale = target_w / w
        target_h = int(h * scale)
    
        
        frame_resized = cv2.resize(frame_rgb, (target_w, target_h))
        img = Image.fromarray(frame_resized)
        imgtk = ImageTk.PhotoImage(image=img)
        
        self.video_label.imgtk = imgtk 
        self.video_label.configure(image=imgtk)

    def execute_play(self):
        if self.ser:
            self.ser.write(b'V') 
          
            self.ser.flush()
            
        self.video_running = True
        self.start_time = time.perf_counter() 
        
        # Set individual calibration flags to True
        self.p1_calibrating = True
        self.p2_calibrating = True
        self.flash_color_state = False
        self.left_canvas.itemconfig(self.status_text, text="CALIBRATING...", fill="yellow")
   
        self.right_canvas.itemconfig(self.p2_status_text, text="CALIBRATING...", fill="yellow")
        
        self.enable_game_ui()
        self._flash_calibrating_text()
        self.play_video_loop()

    def finish_calibration(self, player):
        """Called when a specific player finishes calibrating (sends S1:69 or S2:69)."""
        if player == 1 and self.p1_calibrating:
            self.p1_calibrating = False
          
            self.left_canvas.itemconfig(self.status_text, text="DANCING!", fill="#00BFFF")
        elif player == 2 and self.p2_calibrating:
            self.p2_calibrating = False
            self.right_canvas.itemconfig(self.p2_status_text, text="DANCING!", fill="#FF002F")

    def _flash_calibrating_text(self):
        # Stop flashing if both are done or the game is over
        if not self.game_active or (not self.p1_calibrating and not self.p2_calibrating):
            return
   
        # Toggle between white and yellow
        color = "white" if self.flash_color_state else "yellow"
        self.flash_color_state = not self.flash_color_state
        
        # Only flash the text for the player that is STILL calibrating
        if self.p1_calibrating:
            self.left_canvas.itemconfig(self.status_text, fill=color)
    
        if self.p2_calibrating:
            self.right_canvas.itemconfig(self.p2_status_text, fill=color)
        
        # Call this function again in 300 milliseconds
        self.root.after(300, self._flash_calibrating_text)

    def play_video_loop(self):
        if not self.video_running or self.cap is None:
            return

        current_time = time.perf_counter()
        elapsed = current_time - self.start_time
        
        # Calculate absolute time in the video taking into account the initial offset
        video_time_sec = elapsed + (VIDEO_START_OFFSET_MS / 1000.0)
        
        # --- Check if it's time to show the end screen based on absolute video time ---
        if not self.score_shown and video_time_sec >= self.current_end_time:
         
            self.game_active = False # Stop counting new scores
            self.show_end_screen()
            self.score_shown = True
            
            # Fade out the sidebars
            self.left_sidebar_frame.place_forget()
            self.right_sidebar_frame.place_forget()
            
            self.video_frame.place(relx=0, relwidth=1.0)

        target_frame = int(video_time_sec * self.fps)
        
        if target_frame > self.current_frame:
            frames_to_skip = target_frame - self.current_frame - 1
            
            if frames_to_skip > 30:
                self.cap.set(cv2.CAP_PROP_POS_FRAMES, target_frame)
      
                self.current_frame = target_frame
            elif frames_to_skip > 0:
                for _ in range(frames_to_skip):
                    self.cap.grab()
                    self.current_frame += 1
            
            ret, frame = self.cap.read()
            self.current_frame += 1

            if ret:
                self.display_cv2_frame(frame)
                time_to_next = (1.0 / self.fps) - (time.perf_counter() - current_time)
                delay_ms = int(max(1, time_to_next * 1000))
                self.root.after(delay_ms, self.play_video_loop)
            else:
                self.video_running = False
                self.game_active = False
                
                
                if not self.score_shown:
                    self.show_end_screen()
                    self.score_shown = True
        else:
            time_to_next = (1.0 / self.fps) - (time.perf_counter() - current_time)
            delay_ms = int(max(1, time_to_next * 1000))
           
            self.root.after(delay_ms, self.play_video_loop)

    def enable_game_ui(self):
        self.video_frame.place(relx=0.15, relwidth=0.7)
        self.left_sidebar_frame.place(relx=0, rely=0, relwidth=0.15, relheight=1.0)
        self.right_sidebar_frame.place(relx=0.85, rely=0, relwidth=0.15, relheight=1.0)
        self.game_active = True

    def _clear_feedback(self, player):
        if player == 1:
            self.left_canvas.itemconfig(self.p1_pow_bg, fill="", outline="") 
            self.left_canvas.itemconfig(self.p1_feedback_shadow, text="")
       
            self.left_canvas.itemconfig(self.p1_feedback, text="")
            self.left_canvas.itemconfig(self.p1_num_score_shadow, text="")
            self.left_canvas.itemconfig(self.p1_num_score, text="")
        else:
            self.right_canvas.itemconfig(self.p2_pow_bg, fill="", outline="") 
            self.right_canvas.itemconfig(self.p2_feedback_shadow, text="")
            self.right_canvas.itemconfig(self.p2_feedback, text="")
            self.right_canvas.itemconfig(self.p2_num_score_shadow, text="")
      
            self.right_canvas.itemconfig(self.p2_num_score, text="")

    def show_end_screen(self):
        # Format the final scores for both players
        self.end_p1_score.config(text=f"PLAYER 1: {int(self.p1_total_score):04d} / {MAX_SCORE}")
        self.end_p2_score.config(text=f"PLAYER 2: {int(self.p2_total_score):04d} / {MAX_SCORE}")
        
        # Decide the winner and display it!
        if self.p1_total_score > self.p2_total_score:
            self.end_winner.config(text="🌟 PLAYER 1 WINS! 🌟")
        elif self.p2_total_score > self.p1_total_score:
            self.end_winner.config(text="🌟 PLAYER 2 WINS! 🌟")
        else:
            self.end_winner.config(text="🤝 IT'S A TIE! 🤝")

        self.end_frame.place(relx=0.5, rely=0.5, anchor=tk.CENTER, relwidth=0.5, relheight=0.5)

    def animate_pop(self, canvas, shadow_id, text_id, num_shadow_id, num_score_id, pow_id, cx, cy, is_bad):
    
        """Creates a pop-in effect by stepping the font size down quickly while pulsing a starburst background."""
        # 1. Start extra large
        canvas.itemconfig(shadow_id, font=self.feedback_font_large)
        canvas.itemconfig(text_id, font=self.feedback_font_large)
        canvas.itemconfig(num_shadow_id, font=self.score_font_large)
        canvas.itemconfig(num_score_id, font=self.score_font_large)
        canvas.coords(pow_id, *self._get_pow_coords(cx, cy, 1.25, is_bad))

        # 2. Step to medium
        def step_med():
            canvas.itemconfig(shadow_id, font=self.feedback_font_med)
            canvas.itemconfig(text_id, font=self.feedback_font_med)
            canvas.itemconfig(num_shadow_id, font=self.score_font_med)
            canvas.itemconfig(num_score_id, font=self.score_font_med)
            canvas.coords(pow_id, *self._get_pow_coords(cx, cy, 1.05, is_bad))

        # 3. Rest at small
        def step_small():
           
            canvas.itemconfig(shadow_id, font=self.feedback_font_small)
            canvas.itemconfig(text_id, font=self.feedback_font_small)
            canvas.itemconfig(num_shadow_id, font=self.score_font_small)
            canvas.itemconfig(num_score_id, font=self.score_font_small)
            canvas.coords(pow_id, *self._get_pow_coords(cx, cy, 0.9, is_bad))

        self.root.after(40, step_med)
        self.root.after(90, step_small)

    def update_score(self, player, incoming_score):
        if not self.game_active or self.start_time is None: return 

  
        # --- SCORE DELAY LOGIC ---
        # Stop scores from registering for a player if they are still calibrating
        if player == 1 and self.p1_calibrating: return
        if player == 2 and self.p2_calibrating: return

        # Do absolutely nothing for 0s
        if incoming_score == 0:
            return

     
        # --- Player Specific Custom Colors ---
        if player == 1:
            bad_color = "#FF1493"  
            okay_color = "#1E90FF" 
            okay_burst = "#E0FFFF" 
        else:
            bad_color = "#DC143C"  
          
            okay_color = "#4169E1" 
            okay_burst = "#E6E6FA" 

        word = ""
        color = "#CCCCCC" 
        score_to_add = 0
        score_str = ""
        is_bad = False
        
        # --- Dynamic Scoring & Background Comic Logic ---
     
        if incoming_score in [1, -1]:
            word = random.choice(["NO GOOD ", "YIKES!"])
            color = bad_color
            score_to_add = 0
            score_str = "0 "
            is_bad = True
            burst_fill = "#222222" 
            burst_out = bad_color
        elif incoming_score == 30:
   
            word, color = "PERFECT!", "#FFD700"
            score_to_add = 100
            score_str = "+100 "
            burst_fill = "#FFFF00"
            burst_out = "#FF8C00"
        elif incoming_score >= 100:
            word, color = "PERFECT!", "#FFD700"
            score_to_add = incoming_score
            score_str = f"+{incoming_score} "
            burst_fill = "#FFFF00"
            burst_out = "#FF8C00"
        elif incoming_score >= 90:
            word, color = "AMAZING!", "#FF00FF"
            score_to_add = incoming_score
            score_str = f"+{incoming_score} "
            burst_fill = "#FF69B4" 
            burst_out = "#C71585"
        elif incoming_score >= 80:
            word, color = "GREAT!", "#00FF00"
            score_to_add = incoming_score
            score_str = f"+{incoming_score} "
            burst_fill = "#ADFF2F" 
            burst_out = "#228B22"
        elif incoming_score >= 70 or incoming_score == 10 or incoming_score > 0:
            word, color = "OKAY ", okay_color
     
            score_to_add = incoming_score if incoming_score != 10 else 70
            score_str = f"+{score_to_add} "
            burst_fill = okay_burst
            burst_out = okay_color
        else:
            return

        pow_center_y = 115 

        # --- Update Variables & UI Overlays ---
        if player == 1:
            self.p1_total_score = min(self.p1_total_score + score_to_add, MAX_SCORE)
            current_total = self.p1_total_score
            mask_obj = self.p1_mask
            canvas_obj = self.left_canvas
            star_list, glow_list = self.p1_stars, self.p1_glows
          
            cap_obj = self.p1_cap
            
            # Apply Colors
            self.left_canvas.itemconfig(self.p1_pow_bg, fill=burst_fill, outline=burst_out)
            self.left_canvas.itemconfig(self.p1_feedback_shadow, text=word)
            self.left_canvas.itemconfig(self.p1_feedback, text=word, fill=color)
            self.left_canvas.itemconfig(self.p1_num_score_shadow, text=score_str)
            self.left_canvas.itemconfig(self.p1_num_score, text=score_str, fill=color)
  
            
            # Fire the pop animation
            self.animate_pop(self.left_canvas, self.p1_feedback_shadow, self.p1_feedback, 
                             self.p1_num_score_shadow, self.p1_num_score, self.p1_pow_bg, 
                           
                             self.p_x, pow_center_y, is_bad)

            if self.p1_timer: self.root.after_cancel(self.p1_timer)
            self.p1_timer = self.root.after(1000, lambda: self._clear_feedback(1))
            
        elif player == 2:
            self.p2_total_score = min(self.p2_total_score + score_to_add, MAX_SCORE)
            current_total = self.p2_total_score
            
            mask_obj = self.p2_mask
            canvas_obj = self.right_canvas
            star_list, glow_list = self.p2_stars, self.p2_glows
            cap_obj = self.p2_cap
            
            # Apply Colors
            self.right_canvas.itemconfig(self.p2_pow_bg, fill=burst_fill, outline=burst_out)
            self.right_canvas.itemconfig(self.p2_feedback_shadow, text=word)
 
            self.right_canvas.itemconfig(self.p2_feedback, text=word, fill=color)
            self.right_canvas.itemconfig(self.p2_num_score_shadow, text=score_str)
            self.right_canvas.itemconfig(self.p2_num_score, text=score_str, fill=color)
            
            # Fire the pop animation
            self.animate_pop(self.right_canvas, self.p2_feedback_shadow, self.p2_feedback, 
                 
                             self.p2_num_score_shadow, self.p2_num_score, self.p2_pow_bg, 
                             self.p_x, pow_center_y, is_bad)

            if self.p2_timer: self.root.after_cancel(self.p2_timer)
            self.p2_timer = self.root.after(1000, lambda: self._clear_feedback(2))
        else:
            return

    
        # --- Update the Bar ---
        fill_h = (current_total / MAX_SCORE) * self.bar_h
        mask_bottom = self.bar_bottom - fill_h
        canvas_obj.coords(mask_obj, self.p_x - self.bar_w/2, self.bar_top, self.p_x + self.bar_w/2, mask_bottom)
        
        # Update Cap position
        canvas_obj.coords(cap_obj, self.p_x - self.bar_w/2 - 4, mask_bottom - 3, self.p_x + self.bar_w/2 + 4, mask_bottom + 3)

      
        # --- Flash Animation (The Pop) ---
        if score_to_add > 0:
            flash_rect = canvas_obj.create_rectangle(
                self.p_x - self.bar_w/2, mask_bottom, 
                self.p_x + self.bar_w/2, self.bar_bottom, 
                fill="#FFFFFF", outline=""
          
            )
            self.root.after(80, lambda c=canvas_obj, r=flash_rect: c.delete(r))

        # --- Update Stars ---
        self._update_stars(canvas_obj, current_total, star_list, glow_list)

    def _update_stars(self, canvas, score, star_list, glow_list):
        thresholds = [MAX_SCORE * 0.2, MAX_SCORE * 0.4, MAX_SCORE * 0.6, MAX_SCORE * 0.8, MAX_SCORE * 0.95]
        for i in range(5):
            if score >= thresholds[i]:
                canvas.itemconfig(star_list[i], fill="#FFFFCC") 
                canvas.itemconfig(glow_list[i], fill="#FFCC00") 

    def read_serial(self):
        buffer = ""
        while self.running:
            try:
                if self.ser and self.ser.in_waiting > 0:
       
                    char = self.ser.read(1).decode('utf-8', errors='ignore')
                    if char == '\n':
                        cmd = buffer.strip().upper()
                        buffer = ""
         
                        
                        if cmd == 'X' and self.cap is not None:
                            self.root.after(0, self.execute_play)
                        
                        else:
                            self.process_command(cmd)
                    else:
                        buffer += char
            except Exception:
             
                pass
            time.sleep(0.001) 
            
    def process_command(self, cmd):
        if not cmd: return
        
        if cmd in VIDEOS:
            self.root.after(0, self.load_and_preroll, cmd)
        elif cmd.startswith('S') and ':' in cmd:
         
            try:
                parts = cmd.split(':')
                player_id_str = parts[0][1:] 
                if player_id_str.isdigit():
                    player = int(player_id_str)
                    score = int(float(parts[1]))
                    
                    # If the incoming score is exactly 69, treat it as the calibration trigger!
                    if score == 69:
                        self.root.after(0, self.finish_calibration, player)
                    else:
                        self.root.after(0, self.update_score, player, score)
            except (ValueError, IndexError):
          
                pass

    def on_closing(self):
        self.running = False
        self.video_running = False
        if self.ser:
            self.ser.close()
        if self.cap:
            self.cap.release()
        self.root.destroy()

if __name__ == "__main__":
    root = tk.Tk()
    app = JustDanceGUI(root)
    
    root.protocol("WM_DELETE_WINDOW", app.on_closing) 
    root.mainloop()