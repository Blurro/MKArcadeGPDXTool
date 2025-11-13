using Microsoft.Xna.Framework;
using Microsoft.Xna.Framework.Graphics;
using Microsoft.Xna.Framework.Input;
using System;
using Dark.Net;
using System.Collections.Generic;
using System.Windows;
using System.Windows.Forms;
using System.IO;

namespace MaterialGuiMonogame
{
    public class Game1 : Game
    {
        private GraphicsDeviceManager _graphics;
        private SpriteBatch _spriteBatch;

        private Texture2D _pixel;
        private SpriteFont _font;

        private Dictionary<int, Texture2D> _cornerMaskCache = new Dictionary<int, Texture2D>();

        public Game1()
        {
            _graphics = new GraphicsDeviceManager(this);
            Content.RootDirectory = "Content";
            IsMouseVisible = true;

            _graphics.PreferredBackBufferWidth = 840;
            _graphics.PreferredBackBufferHeight = 470;
            Window.Title = "Material Editor";
            _graphics.PreferMultiSampling = true;
            _graphics.GraphicsProfile = GraphicsProfile.HiDef;
            _graphics.ApplyChanges();
        }

        private RenderTarget2D _buttonRenderTarget;
        private RenderTarget2D _barRenderTarget;
        protected override void LoadContent()
        {
            _spriteBatch = new SpriteBatch(GraphicsDevice);

            _font = Content.Load<SpriteFont>("NintyFont");

            // all rounded rects use this 1x1 white pixel texture
            _pixel = new Texture2D(GraphicsDevice, 1, 1);
            _pixel.SetData(new[] { Color.White });

            // --- create rounded rect button texture ---
            // Create a RenderTarget2D for the button
            int targetWidth = _buttonBaseWidth * 2; // double for smooth downscale
            int targetHeight = _buttonBaseHeight * 2;
            _buttonRenderTarget = new RenderTarget2D(GraphicsDevice, targetWidth, targetHeight);
            // Draw the rounded rectangle to the RenderTarget
            GraphicsDevice.SetRenderTarget(_buttonRenderTarget);
            GraphicsDevice.Clear(Color.Transparent);
            _spriteBatch.Begin();
            DrawRoundedRect(new Rectangle(0, 0, targetWidth, targetHeight), Color.White, _buttonCornerRadiusBase * 2);
            _spriteBatch.End();
            GraphicsDevice.SetRenderTarget(null);

            // --- create transitionSquare texture ---
            int squareSize = _transitionSquareSize;
            int rtSize = squareSize * 2; // oversize render target so corners don’t get clipped
            var squareTarget = new RenderTarget2D(GraphicsDevice, rtSize, rtSize);
            GraphicsDevice.SetRenderTarget(squareTarget);
            GraphicsDevice.Clear(Color.Transparent);
            _spriteBatch.Begin();
            // draw a white square centered in the larger target, rotated
            Vector2 rtCenter = new Vector2(rtSize / 2f, rtSize / 2f);
            Rectangle rect = new Rectangle(
                 (rtSize - squareSize) / 2,  // offset so it's centered
                 (rtSize - squareSize) / 2,
                 squareSize,
                 squareSize
             );
            DrawRoundedRect(rect, Color.White, _buttonCornerRadiusBase);
            _spriteBatch.End();
            GraphicsDevice.SetRenderTarget(null);
            _transitionSquareTexture = squareTarget;
            // base anim state
            _transitionBasePos = new Vector2(0, _graphics.PreferredBackBufferHeight - 20);
            // initialize clone scales (all start at zero)
            _transitionCloneScales = new float[_transitionCloneCount];
            for (int i = 0; i < _transitionCloneCount; i++)
                _transitionCloneScales[i] = 0f;
            _transitionCloneShrinking = new bool[_transitionCloneCount];
            for (int i = 0; i < _transitionCloneCount; i++)
                _transitionCloneShrinking[i] = false;
            _transitionAnimTime = 0f;

            // --- create dropdown bar texture ---
            // Create a RenderTarget2D for the bar
            targetWidth = 630 * 2; // double for smooth downscale
            targetHeight = 45 * 2;
            _barRenderTarget = new RenderTarget2D(GraphicsDevice, targetWidth, targetHeight);
            // Draw the rounded rectangle to the RenderTarget
            GraphicsDevice.SetRenderTarget(_barRenderTarget);
            GraphicsDevice.Clear(Color.Transparent);
            _barRenderTarget = CreateHollowRoundedRect(GraphicsDevice, targetWidth, targetHeight, 15, 6);
            GraphicsDevice.SetRenderTarget(null);

            // --- create buttonSelect texture ---
            int selectTargetWidth = _buttonSelectWidth * 2;
            int selectTargetHeight = _buttonSelectHeight * 2;
            _buttonSelectRenderTarget = new RenderTarget2D(GraphicsDevice, selectTargetWidth, selectTargetHeight);
            GraphicsDevice.SetRenderTarget(_buttonSelectRenderTarget);
            GraphicsDevice.Clear(Color.Transparent);
            _spriteBatch.Begin();
            DrawRoundedRect(
                new Rectangle(0, 0, selectTargetWidth, selectTargetHeight),
                _buttonSelectColor,
                _buttonSelectCornerRadius * 2
            );
            DrawRoundedRect(
                new Rectangle(0, -6, selectTargetWidth, selectTargetHeight),
                new Color(0x28, 0x28, 0x28),
                _buttonSelectCornerRadius * 2
            );
            _spriteBatch.End();
            GraphicsDevice.SetRenderTarget(null);

            // --- create buttonApply texture ---
            int applyTargetWidth = _buttonApplyWidth * 2;
            int applyTargetHeight = _buttonApplyHeight * 2;
            _buttonApplyRenderTarget = new RenderTarget2D(GraphicsDevice, applyTargetWidth, applyTargetHeight);
            GraphicsDevice.SetRenderTarget(_buttonApplyRenderTarget);
            GraphicsDevice.Clear(Color.Transparent);
            _spriteBatch.Begin();
            DrawRoundedRect(
                new Rectangle(0, 0, applyTargetWidth, applyTargetHeight),
                _buttonApplyColor,
                _buttonApplyCornerRadius * 2
            );
            DrawRoundedRect(
                new Rectangle(0, -6, applyTargetWidth, applyTargetHeight),
                new Color(0x28, 0x28, 0x28),
                _buttonApplyCornerRadius * 2
            );
            _spriteBatch.End();
            GraphicsDevice.SetRenderTarget(null);

            // --- create buttonSave texture ---
            int saveTargetWidth = _buttonSaveWidth * 2;
            int saveTargetHeight = _buttonSaveHeight * 2;
            _buttonSaveRenderTarget = new RenderTarget2D(GraphicsDevice, saveTargetWidth, saveTargetHeight);
            GraphicsDevice.SetRenderTarget(_buttonSaveRenderTarget);
            GraphicsDevice.Clear(Color.Transparent);
            _spriteBatch.Begin();
            DrawRoundedRect(
                new Rectangle(0, 0, saveTargetWidth, saveTargetHeight),
                _buttonSaveColor,
                _buttonSaveCornerRadius * 2
            );
            DrawRoundedRect(
                new Rectangle(0, -6, saveTargetWidth, saveTargetHeight),
                new Color(0x28, 0x28, 0x28),
                _buttonSaveCornerRadius * 2
            );
            _spriteBatch.End();
            GraphicsDevice.SetRenderTarget(null);

            // initial Dark.Net setup
            var form = (System.Windows.Forms.Form)System.Windows.Forms.Control.FromHandle(Window.Handle);
            if (form != null)
            {
                var options = new Dark.Net.ThemeOptions
                {
                    TitleBarBackgroundColor = System.Drawing.Color.FromArgb(40, 40, 40),
                    TitleBarTextColor = System.Drawing.Color.White,
                };
                Dark.Net.DarkNet.Instance.SetWindowThemeForms(form, Dark.Net.Theme.Dark, options);
            }

            // REMOVE PLAN LATER
            _planTexture = Content.Load<Texture2D>("plan");
        }

        // A BUNCH OF STARTING DEFINITIONS UP UNTIL UPDATE FUNC

        // REMOVE PLAN LATER
        private Texture2D _planTexture;

        //cool border
        private float _borderHue = 230f;
        private double _timeSinceLastUpdate = 0;
        private double _updateInterval = 0.1; // update every 0.1s
        private double _fullCycleSeconds = 60; // full hue cycle duration

        //bar
        private float _barScale = 0.5f;
        private Vector2 _barOffset = new Vector2(-85, -200);
        private Vector2 _barPos => new Vector2(
            (GraphicsDevice.Viewport.Width - _barRenderTarget.Width * _barScale) / 2 + _barOffset.X,
            (GraphicsDevice.Viewport.Height - _barRenderTarget.Height * _barScale) / 2 + _barOffset.Y
        );

        //button
        private Color _buttonColor = new Color(0x68, 0x68, 0x68); // default
        private Color _hoverColor = new Color(0x55, 0x59, 0x89);
        private Color _pressedColor = new Color(0x3A, 0x3F, 0x4D);
        private float _buttonScale = 0.2f; // start teeny for pop in animate
        private float _buttonTxtScale = 0f;
        // shared button properties
        private Vector2 _buttonOffset = new Vector2(0, 85); // x offset, y offset
        private Vector2 _buttonPos => new Vector2(
            (GraphicsDevice.Viewport.Width - _buttonRenderTarget.Width * _buttonScale) / 2 + _buttonOffset.X,
            (GraphicsDevice.Viewport.Height - _buttonRenderTarget.Height * _buttonScale) / 2 + _buttonOffset.Y
        );
        private Vector2 _buttonSize => new Vector2(_buttonRenderTarget.Width * _buttonScale, _buttonRenderTarget.Height * _buttonScale);
        // hitbox rectangle (update uses this)
        private Rectangle _buttonRect => new Rectangle(
            (int)_buttonPos.X,
            (int)_buttonPos.Y,
            (int)_buttonSize.X,
            (int)_buttonSize.Y
        );
        // width height
        private int _buttonBaseWidth => 695;
        private int _buttonBaseHeight => 205;
        // base corner radius in pixels
        private int _buttonCornerRadiusBase = 30;
        // current scaled corner radius
        private int _buttonCornerRadius => (int)(_buttonCornerRadiusBase * _buttonScale);

        // --- buttonSelect ---
        private RenderTarget2D _buttonSelectRenderTarget;
        private Vector2 _buttonSelectPos = new Vector2(650, 100);
        private int _buttonSelectWidth = 190;
        private int _buttonSelectHeight = 70;
        private int _buttonSelectCornerRadius = 20;
        private float _buttonSelectScale = 0.7f;
        private Color _buttonSelectColor = new Color(157, 157, 157);

        // --- buttonApply ---
        private RenderTarget2D _buttonApplyRenderTarget;
        private Vector2 _buttonApplyPos = new Vector2(650, 200);
        private int _buttonApplyWidth = 170;
        private int _buttonApplyHeight = 70;
        private int _buttonApplyCornerRadius = 20;
        private float _buttonApplyScale = 1f;
        private Color _buttonApplyColor = new Color(157, 157, 157);

        // --- buttonSave ---
        private RenderTarget2D _buttonSaveRenderTarget;
        private Vector2 _buttonSavePos = new Vector2(650, 300);
        private int _buttonSaveWidth = 170;
        private int _buttonSaveHeight = 70;
        private int _buttonSaveCornerRadius = 20;
        private float _buttonSaveScale = 1f;
        private Color _buttonSaveColor = new Color(157, 157, 157);

        // for transition square
        private int _transitionSquareSize = 130;
        private float _transitionSquareRotationDeg = 20f;
        private Texture2D _transitionSquareTexture;
        // grid config
        private int _transitionCloneCols = 10;
        private int _transitionCloneRows = 6;
        private int _transitionCloneSpacingX = 100;
        private int _transitionCloneSpacingY = -100;
        // derived
        private int _transitionCloneCount => _transitionCloneCols * _transitionCloneRows;
        // base animation state (animated in Update)
        private Vector2 _transitionBasePos = new Vector2(0, 0); // random not actually used
        private float[] _transitionCloneScales;
        private bool[] _transitionCloneShrinking;
        private float _transitionAnimTime = 0f;

        private System.Drawing.Color _titleBarBgColor = System.Drawing.Color.FromArgb(40, 40, 40);
        private float _titleBarLerpTime = 0f;
        // current theme color (System.Drawing for Dark.Net, XNA Color for drawing)
        private System.Drawing.Color _currentThemeColorSys = System.Drawing.Color.FromArgb(40, 40, 40);
        private Microsoft.Xna.Framework.Color _currentThemeColor = new Microsoft.Xna.Framework.Color(40, 40, 40);
        // convenience rad value for rotated draw
        private float _transitionSquareRotationRad => MathHelper.ToRadians(_transitionSquareRotationDeg);

        private bool _button1Pressed = false;
        private bool _buttonHoverReleased = true;

        private short _currentScreen = 1;
        private short _titleDone = 0;
        private string _selectedFilePath = null;
        private bool updatedOnce = false;
        protected override void Update(GameTime gameTime)
        {
            if (_currentScreen == 3 && Keyboard.GetState().IsKeyDown(Microsoft.Xna.Framework.Input.Keys.Escape))
            {
                var result = System.Windows.Forms.MessageBox.Show(
                    "Are you sure? You may lose any unsaved progress!",
                    "Return to Menu",
                    System.Windows.Forms.MessageBoxButtons.YesNo,
                    System.Windows.Forms.MessageBoxIcon.None
                );
                if (result == System.Windows.Forms.DialogResult.Yes)
                {
                    // go back to screen 1
                    updatedOnce = false;
                    _currentScreen = 1;
                    _titleDone = 0;
                    _selectedFilePath = null;
                    _transitionAnimTime = 0f;
                    _titleBarLerpTime = 0f;
                    _transitionCloneScales = new float[_transitionCloneCount];
                    for (int i = 0; i < _transitionCloneCount; i++)
                        _transitionCloneScales[i] = 0f;
                    _transitionCloneShrinking = new bool[_transitionCloneCount];
                    for (int i = 0; i < _transitionCloneCount; i++)
                        _transitionCloneShrinking[i] = false;
                    _buttonScale = 0.2f;
                    _buttonTxtScale = 0f;
                }
            }
            //Console.WriteLine(_currentScreen);

            if (_currentScreen == 1 && updatedOnce == false)
            {
                updatedOnce = true;
                _fullCycleSeconds = 60;
                Window.Title = "Material Editor";
            } else if (_currentScreen == 3 && updatedOnce == false)
            {
                updatedOnce = true;
                _fullCycleSeconds = 180;
                Window.Title = "Material Editor - Press ESC to go back";
            }

            var mouse = Mouse.GetState();
            bool isWindowFocused = this.IsActive;

            bool isHoveredBtn1 = false;
            if (_selectedFilePath == null && _currentScreen == 1)
            {
                isHoveredBtn1 = IsMouseInRoundedRect(_buttonRect, (int)(_buttonCornerRadius), mouse.Position);
            }

            bool mouseDown = mouse.LeftButton == Microsoft.Xna.Framework.Input.ButtonState.Pressed;
            bool mouseUp = mouse.LeftButton == Microsoft.Xna.Framework.Input.ButtonState.Released;

            // detect hover release
            if (!isHoveredBtn1)
                _buttonHoverReleased = true;

            // update button pressed state
            if (_button1Pressed)
            {
                if (mouseUp)
                {
                    _button1Pressed = false;
                    if (isHoveredBtn1 && isWindowFocused && _buttonHoverReleased)
                    {
                        _buttonHoverReleased = false;
                        var dialog = new OpenFileDialog
                        {
                            Filter = "BIN and TXT files|*.bin;*.txt",
                            Title = "Select a file",
                            Multiselect = false
                        };

                        var result = dialog.ShowDialog();
                        if (result == DialogResult.OK)
                        {
                            _selectedFilePath = dialog.FileName;
                            Console.WriteLine("Selected: " + _selectedFilePath);

                            var materials = MaterialPresetLoader.LoadPreset(_selectedFilePath);

                            if (materials == null || materials.Count == 0)
                            {
                                System.Windows.Forms.MessageBox.Show(
                                    "No materials were loaded from the selected file.",
                                    "Load Error",
                                    MessageBoxButtons.OK,
                                    MessageBoxIcon.Error
                                );

                                _selectedFilePath = null;
                                return;
                            }

                            Console.WriteLine("Loaded " + materials.Count + " materials.");
                        }
                    }
                }
            }
            else
            { // must un hover before being able to re click
                if (isHoveredBtn1 && mouseDown && isWindowFocused && _buttonHoverReleased)
                    _button1Pressed = true;
            }

            // determine target color
            Color targetColor = _button1Pressed ? _pressedColor : (isHoveredBtn1 ? _hoverColor : new Color(0x68, 0x68, 0x68));

            // time since last frame
            float dt = (float)gameTime.ElapsedGameTime.TotalSeconds;

            // --- color lerp (0.1s duration) ---
            float colorDuration = 0.1f;
            float colorLerpFactor = MathHelper.Clamp(dt / colorDuration, 0f, 1f);
            float alpha = 1f - (float)Math.Pow(0.1f, gameTime.ElapsedGameTime.TotalSeconds / 0.1f);
            _buttonColor = Color.Lerp(_buttonColor, targetColor, alpha);

            // --- scale lerp (0.5s duration) ---
            float scaleDuration = 0.5f;
            float scaleLerpFactor = MathHelper.Clamp(dt / scaleDuration, 0f, 1f);
            alpha = 1f - (float)Math.Pow(0.02f, gameTime.ElapsedGameTime.TotalSeconds / 0.5f);
            _buttonScale = MathHelper.Lerp(_buttonScale, _button1Pressed ? 0.5f * 0.98f : 0.5f, alpha);
            float targetTxtScale = _button1Pressed ? 0.6f * 0.98f : (isHoveredBtn1 ? 0.65f : 0.6f);
            _buttonTxtScale = MathHelper.Lerp(_buttonTxtScale, targetTxtScale, alpha);

            if (_selectedFilePath != null && _titleDone != 2)
            {
                float animSpeed = 1f;
                //if (Keyboard.GetState().IsKeyDown(Microsoft.Xna.Framework.Input.Keys.M)) animSpeed = 0.05f;
                _transitionAnimTime += (float)gameTime.ElapsedGameTime.TotalSeconds * animSpeed;

                // debug prints to watch timing

                if (_transitionAnimTime >= 1.58f && _currentScreen == 1)
                {
                    _currentScreen = 2;
                    //Console.WriteLine(">>> switched to screen 2 at " + _transitionAnimTime.ToString("F3") + "s");
                }

                for (int i = 0; i < _transitionCloneCount; i++)
                {
                    float delay = i * 0.014f;
                    float t;

                    if (!_transitionCloneShrinking[i] && _transitionAnimTime >= 1.32f + delay)
                        _transitionCloneShrinking[i] = true;

                    if (_transitionCloneShrinking[i])
                    {
                        float shrinkTime = _transitionAnimTime - (1.36f + delay);
                        t = MathHelper.Clamp((0.7f - shrinkTime) / 0.7f, 0f, 1f);
                        _transitionCloneScales[i] = MathHelper.Lerp(0f, 1f, t);
                    }
                    else
                    {
                        t = MathHelper.Clamp((_transitionAnimTime - delay) / 0.7f, 0f, 1f);
                        _transitionCloneScales[i] = MathHelper.Lerp(0f, 1f, t);
                    }
                }

                if (_transitionAnimTime >= 2.4f && _titleDone == 0)
                {
                    _titleDone = 1;
                    _titleBarLerpTime = 0f;
                }
                if (_transitionAnimTime >= 2.8f && _titleDone == 1)
                {
                    _titleDone = 2;
                    _updateInterval = 0.1f;
                    _currentScreen = 3;
                    updatedOnce = false;
                }
                // --- title bar lerp ---
                float animDuration = 0.3f;
                if (_transitionAnimTime >= 1f && _titleDone == 0)
                {
                    _updateInterval = 0f;
                    _titleBarLerpTime += (float)gameTime.ElapsedGameTime.TotalSeconds;
                    float tt = MathHelper.Clamp(_titleBarLerpTime / animDuration, 0f, 1f);

                    var start = System.Drawing.Color.FromArgb(40, 40, 40);
                    var target = _currentThemeColorSys;

                    _titleBarBgColor = System.Drawing.Color.FromArgb(
                        (int)MathHelper.Lerp(start.R, target.R, tt),
                        (int)MathHelper.Lerp(start.G, target.G, tt),
                        (int)MathHelper.Lerp(start.B, target.B, tt)
                    );
                }
                // --- shrink back to grey ---
                else if (_titleDone == 1)
                {
                    _titleBarLerpTime += (float)gameTime.ElapsedGameTime.TotalSeconds;
                    float tt = MathHelper.Clamp(_titleBarLerpTime / animDuration, 0f, 1f);

                    var start = _currentThemeColorSys;
                    var target = System.Drawing.Color.FromArgb(40, 40, 40);

                    _titleBarBgColor = System.Drawing.Color.FromArgb(
                        (int)MathHelper.Lerp(start.R, target.R, tt),
                        (int)MathHelper.Lerp(start.G, target.G, tt),
                        (int)MathHelper.Lerp(start.B, target.B, tt)
                    );
                }
            }

            // --- border + theme color logic ---
            _timeSinceLastUpdate += gameTime.ElapsedGameTime.TotalSeconds;
            if (_timeSinceLastUpdate >= _updateInterval)
            {
                float step = (360f / (float)_fullCycleSeconds) * (float)_updateInterval;
                _borderHue = (_borderHue + step) % 360f;

                var colorSys = HsvToRgb(_borderHue, 0.95f, 0.5f);
                _currentThemeColorSys = colorSys;
                _currentThemeColor = new Microsoft.Xna.Framework.Color(colorSys.R, colorSys.G, colorSys.B);

                var form = System.Windows.Forms.Control.FromHandle(Window.Handle) as System.Windows.Forms.Form;
                if (form != null)
                {
                    var options = new Dark.Net.ThemeOptions
                    {
                        TitleBarBackgroundColor = _titleBarBgColor,
                        TitleBarTextColor = System.Drawing.Color.White,
                        WindowBorderColor = _currentThemeColorSys
                    };
                    Dark.Net.DarkNet.Instance.SetWindowThemeForms(form, Dark.Net.Theme.Dark, options);
                }

                _timeSinceLastUpdate = 0;
            }

            base.Update(gameTime);
        }

        protected override void Draw(GameTime gameTime)
        {
            GraphicsDevice.Clear(new Color(0x28, 0x28, 0x28));

            _spriteBatch.Begin(samplerState: SamplerState.LinearClamp);

            if (_currentScreen == 1)
            {
                // title
                DrawItalicString(
                    _spriteBatch,
                    _font,
                    "Material Preset Editor",
                    new Vector2((GraphicsDevice.Viewport.Width - _font.MeasureString("Material Preset Editor").X) / 2, GraphicsDevice.Viewport.Height * 0.15f + 5),
                    Color.Snow,
                    0.25f,   // skew strength
                    1.08f   // stretch a bit
                );

                // subtitle
                var subtitle = "To get started, load a model's extracted preset data";
                float subScale = 0.5f;
                var subtitleSize = _font.MeasureString(subtitle) * subScale;
                _spriteBatch.DrawString(
                    _font,
                    subtitle,
                    new Vector2((GraphicsDevice.Viewport.Width - subtitleSize.X) / 2, GraphicsDevice.Viewport.Height * 0.3f + 15),
                    Color.Gold,
                    0f,
                    Vector2.Zero,
                    subScale,
                    SpriteEffects.None,
                    0f
                );

                // draw the button
                _spriteBatch.Draw(_buttonRenderTarget, _buttonPos, null, _buttonColor, 0f, Vector2.Zero, _buttonScale, SpriteEffects.None, 0f);

                // draw button text
                var btnText = "Browse for a file...";
                Vector2 btnTextSize = _font.MeasureString(btnText) * _buttonTxtScale;
                Vector2 textPos = new Vector2(
                    _buttonPos.X + (_buttonSize.X - btnTextSize.X) / 2f,
                    _buttonPos.Y + (_buttonSize.Y - btnTextSize.Y) / 2f
                );
                _spriteBatch.DrawString(
                    _font,
                    btnText,
                    textPos,
                    Color.White,
                    0f,
                    Vector2.Zero,
                    _buttonTxtScale,
                    SpriteEffects.None,
                    0f
                );
            } else if (_currentScreen == 2 || _currentScreen == 3)
            {
                // REMOVE PLAN LATER
                _spriteBatch.Draw(_planTexture, new Vector2(-27, -60), Color.White);

                Color BlueBoost(Color color, float maxBoost = 0.5f, float baseBoost = 0.25f)
                {
                    float r = color.R / 255f;
                    float g = color.G / 255f;
                    float b = color.B / 255f;

                    float max = Math.Max(r, Math.Max(g, b));
                    float min = Math.Min(r, Math.Min(g, b));
                    float delta = max - min;

                    float h = 0f;
                    if (delta > 0f)
                    {
                        if (max == r) h = (g - b) / delta % 6f;
                        else if (max == g) h = (b - r) / delta + 2f;
                        else h = (r - g) / delta + 4f;
                        h *= 60f;
                        if (h < 0) h += 360f;
                    }

                    // smooth, wider falloff for blue starting closer to purple
                    float boost = baseBoost; // base brightness for all hues
                    float startH = 130f;
                    float peakStart = 220f; // blue range start
                    float peakEnd = 270f; // blue range end
                    float fadeEnd = 360f; // fade back to base at pink-ish

                    if (h >= startH && h < peakStart) boost = baseBoost + maxBoost * (h - startH) / (peakStart - startH);
                    else if (h >= peakStart && h <= peakEnd) boost = baseBoost + maxBoost;
                    else if (h > peakEnd && h <= fadeEnd) boost = baseBoost + maxBoost * (1f - (h - peakEnd) / (fadeEnd - peakEnd));

                    // convert to HSV and adjust value
                    float v = max;
                    v = Math.Min(v + boost, 1f);

                    float c = v - min;
                    float x = c * (1 - Math.Abs((h / 60f) % 2 - 1));
                    float m = min;

                    float r1 = 0, g1 = 0, b1 = 0;
                    if (h < 60f) { r1 = c; g1 = x; b1 = 0; }
                    else if (h < 120f) { r1 = x; g1 = c; b1 = 0; }
                    else if (h < 180f) { r1 = 0; g1 = c; b1 = x; }
                    else if (h < 240f) { r1 = 0; g1 = x; b1 = c; }
                    else if (h < 300f) { r1 = x; g1 = 0; b1 = c; }
                    else { r1 = c; g1 = 0; b1 = x; }

                    r = r1 + m;
                    g = g1 + m;
                    b = b1 + m;

                    return new Color((int)(r * 255f), (int)(g * 255f), (int)(b * 255f), color.A);
                }
                var vibrantColor = BlueBoost(_currentThemeColor);
                _spriteBatch.Draw(_barRenderTarget, _barPos, null, vibrantColor, 0f, Vector2.Zero, _barScale, SpriteEffects.None, 0f);

                float _buttonTextScale = 0.4f;

                _spriteBatch.Draw(_buttonSelectRenderTarget, _buttonSelectPos, null, Color.White, 0f, Vector2.Zero, _buttonSelectScale * 0.5f, SpriteEffects.None, 0f);
                string l1 = "Select Folder";
                string l2 = "to load images";
                float s = _buttonTextScale;
                float g = 8f;
                Vector2 s1 = _font.MeasureString(l1) * s;
                Vector2 s2 = _font.MeasureString(l2) * s;
                float bx = _buttonSelectPos.X;
                float by = _buttonSelectPos.Y -5;
                float w = _buttonSelectWidth;
                float h = _buttonSelectHeight;
                _spriteBatch.DrawString(_font, l1, new Vector2(bx + (w - s1.X) / 2f, by + (h - (s1.Y + s2.Y)) / 2f), Color.White, 0f, Vector2.Zero, s, SpriteEffects.None, 0f);
                _spriteBatch.DrawString(_font, l2, new Vector2(bx + (w - s2.X) / 2f, by + (h - (s1.Y + s2.Y)) / 2f + s1.Y + g), Color.White, 0f, Vector2.Zero, s, SpriteEffects.None, 0f);

                _spriteBatch.Draw(_buttonApplyRenderTarget, _buttonApplyPos, null, Color.White, 0f, Vector2.Zero, _buttonApplyScale * 0.5f, SpriteEffects.None, 0f);
                _spriteBatch.DrawString(_font, "Apply to BIN", new Vector2(_buttonApplyPos.X + (_buttonApplyWidth - _font.MeasureString("Apply to BIN").X * _buttonTextScale) / 2f, _buttonApplyPos.Y + (_buttonApplyHeight - _font.MeasureString("Apply to BIN").Y * _buttonTextScale + 15) / 2f + 5), Color.White, 0f, Vector2.Zero, _buttonTextScale, SpriteEffects.None, 0f);

                _spriteBatch.Draw(_buttonSaveRenderTarget, _buttonSavePos, null, Color.White, 0f, Vector2.Zero, _buttonSaveScale * 0.5f, SpriteEffects.None, 0f);
                _spriteBatch.DrawString(_font, "Save Preset", new Vector2(_buttonSavePos.X + (_buttonSaveWidth - _font.MeasureString("Save Preset").X * _buttonTextScale) / 2f, _buttonSavePos.Y + (_buttonSaveHeight - _font.MeasureString("Save Preset").Y * _buttonTextScale + 15) / 2f + 5), Color.White, 0f, Vector2.Zero, _buttonTextScale, SpriteEffects.None, 0f);
            }

            // draw transition square clones
            int idx = 0;
            for (int row = 0; row < _transitionCloneRows; row++)
            {
                for (int col = 0; col < _transitionCloneCols; col++)
                {
                    Vector2 offset = new Vector2(col * _transitionCloneSpacingX, row * _transitionCloneSpacingY);
                    Vector2 pos = _transitionBasePos + offset;

                    float scale = 0f;
                    if (_transitionCloneScales != null && idx < _transitionCloneScales.Length)
                        scale = _transitionCloneScales[idx];

                    _spriteBatch.Draw(
                        _transitionSquareTexture,
                        pos,
                        null,
                        _currentThemeColor,
                        _transitionSquareRotationRad,
                        new Vector2(_transitionSquareSize / 2f, _transitionSquareSize / 2f),
                        scale,
                        SpriteEffects.None,
                        0f
                    );
                    idx++;
                }
            }

            _spriteBatch.End();

            base.Draw(gameTime);
        }

        void DrawItalicString(SpriteBatch sb, SpriteFont font, string text, Vector2 pos, Color color, float skew, float stretch = 1.05f, int boldOffset = 1)
        {
            var size = font.MeasureString(text);
            var center = new Vector3(pos.X + size.X / 2, pos.Y + size.Y / 2, 0);

            var skewMatrix =
                Matrix.CreateTranslation(-center) *
                new Matrix(
                    stretch, 0, 0, 0,
                    -skew, 1, 0, 0,
                    0, 0, 1, 0,
                    0, 0, 0, 1
                ) *
                Matrix.CreateTranslation(center);

            sb.End();

            sb.Begin(transformMatrix: skewMatrix, blendState: BlendState.AlphaBlend, samplerState: SamplerState.LinearClamp);

            // draw main text
            sb.DrawString(font, text, pos, color);

            // draw offset pass for "bold" effect
            sb.DrawString(font, text, pos + new Vector2(boldOffset, 0), color);

            sb.End();

            sb.Begin();
        }

        private System.Drawing.Color HsvToRgb(float h, float s, float v)
        {
            float c = v * s;
            float x = c * (1 - Math.Abs((h / 60f) % 2 - 1));
            float m = v - c;

            float r1 = 0, g1 = 0, b1 = 0;
            if (h < 60) { r1 = c; g1 = x; b1 = 0; }
            else if (h < 120) { r1 = x; g1 = c; b1 = 0; }
            else if (h < 180) { r1 = 0; g1 = c; b1 = x; }
            else if (h < 240) { r1 = 0; g1 = x; b1 = c; }
            else if (h < 300) { r1 = x; g1 = 0; b1 = c; }
            else { r1 = c; g1 = 0; b1 = x; }

            return System.Drawing.Color.FromArgb(
                (int)((r1 + m) * 255),
                (int)((g1 + m) * 255),
                (int)((b1 + m) * 255)
            );
        }

        private Texture2D GetCornerMask(int radius)
        {
            if (radius <= 0)
                return null;
            if (_cornerMaskCache.TryGetValue(radius, out var tex))
                return tex;
            // build mask
            int size = radius * 2;
            var mask = new Texture2D(GraphicsDevice, size, size);
            var data = new Color[size * size];
            for (int y = 0; y < size; y++)
            {
                for (int x = 0; x < size; x++)
                {
                    int dx = x - radius;
                    int dy = y - radius;
                    // optionally anti alias by fading alpha near border
                    float distSq = dx * dx + dy * dy;
                    float max = radius * radius;
                    if (distSq <= max)
                        data[y * size + x] = Color.White;
                    else
                        data[y * size + x] = Color.Transparent;
                }
            }
            mask.SetData(data);
            _cornerMaskCache[radius] = mask;
            return mask;
        }

        private void DrawRoundedRect(Rectangle rect, Color color, int radius = 12, float scale = 1f)
        {
            // Apply scaling to the rectangle dimensions
            int scaledWidth = (int)(rect.Width * scale);
            int scaledHeight = (int)(rect.Height * scale);
            int scaledRadius = (int)(radius * scale);

            // Clamp the scaled radius to ensure it doesn't exceed half the scaled dimensions
            scaledRadius = Math.Min(scaledRadius, Math.Min(scaledWidth, scaledHeight) / 2);

            // Create a new rectangle with the scaled dimensions
            var scaledRect = new Rectangle(
                rect.X + (rect.Width - scaledWidth) / 2,
                rect.Y + (rect.Height - scaledHeight) / 2,
                scaledWidth,
                scaledHeight
            );

            // Get the corner mask for the scaled radius
            var mask = GetCornerMask(scaledRadius);
            if (scaledRadius <= 0 || mask == null)
            {
                _spriteBatch.Draw(_pixel, scaledRect, color);
                return;
            }

            // Center body
            var centerRect = new Rectangle(scaledRect.X + scaledRadius, scaledRect.Y, scaledRect.Width - 2 * scaledRadius, scaledRect.Height);
            _spriteBatch.Draw(_pixel, centerRect, color);

            // Left/right vertical bars
            _spriteBatch.Draw(_pixel, new Rectangle(scaledRect.X, scaledRect.Y + scaledRadius, scaledRadius, scaledRect.Height - 2 * scaledRadius), color);
            _spriteBatch.Draw(_pixel, new Rectangle(scaledRect.Right - scaledRadius, scaledRect.Y + scaledRadius, scaledRadius, scaledRect.Height - 2 * scaledRadius), color);

            // Corners using mask
            // Top-left
            _spriteBatch.Draw(mask, new Vector2(scaledRect.X, scaledRect.Y), null, color, 0f, Vector2.Zero, 1f, SpriteEffects.None, 0f);
            // Top-right
            _spriteBatch.Draw(mask, new Vector2(scaledRect.Right - scaledRadius * 2, scaledRect.Y), null, color, 0f, Vector2.Zero, 1f, SpriteEffects.FlipHorizontally, 0f);
            // Bottom-left
            _spriteBatch.Draw(mask, new Vector2(scaledRect.X, scaledRect.Bottom - scaledRadius * 2), null, color, 0f, Vector2.Zero, 1f, SpriteEffects.FlipVertically, 0f);
            // Bottom-right
            _spriteBatch.Draw(mask, new Vector2(scaledRect.Right - scaledRadius * 2, scaledRect.Bottom - scaledRadius * 2), null, color, 0f, Vector2.Zero, 1f, SpriteEffects.FlipHorizontally | SpriteEffects.FlipVertically, 0f);
        }

        private RenderTarget2D CreateHollowRoundedRect(GraphicsDevice device, int width, int height, int radius, int thickness)
        {
            var target = new RenderTarget2D(device, width, height);
            device.SetRenderTarget(target);
            device.Clear(Color.Transparent);

            // --- pass 1: draw outer rect filled white ---
            _spriteBatch.Begin();
            DrawRoundedRect(new Rectangle(0, 0, width, height), Color.White, radius);
            _spriteBatch.End();

            // --- pass 2: erase inner rect using custom blend state ---
            var eraseBlend = new BlendState
            {
                ColorSourceBlend = Blend.Zero,
                AlphaSourceBlend = Blend.Zero,
                ColorDestinationBlend = Blend.InverseSourceAlpha,
                AlphaDestinationBlend = Blend.InverseSourceAlpha
            };
            _spriteBatch.Begin(SpriteSortMode.Immediate, eraseBlend);
            DrawRoundedRect(
                new Rectangle(thickness, thickness, width - thickness * 2, height - thickness * 2),
                Color.White, // solid white, but blend erases instead of adding
                Math.Max(0, radius - thickness)
            );
            _spriteBatch.End();

            device.SetRenderTarget(null);
            return target;
        }

        private bool IsMouseInRoundedRect(Rectangle rect, int radius, Microsoft.Xna.Framework.Point mouse)
        {
            // first, quick bounding box check
            if (!rect.Contains(mouse))
                return false;

            // check corners
            int left = rect.X;
            int right = rect.Right;
            int top = rect.Y;
            int bottom = rect.Bottom;

            // top-left
            if (mouse.X < left + radius && mouse.Y < top + radius)
                return (mouse.X - (left + radius)) * (mouse.X - (left + radius)) + (mouse.Y - (top + radius)) * (mouse.Y - (top + radius)) <= radius * radius;

            // top-right
            if (mouse.X > right - radius && mouse.Y < top + radius)
                return (mouse.X - (right - radius)) * (mouse.X - (right - radius)) + (mouse.Y - (top + radius)) * (mouse.Y - (top + radius)) <= radius * radius;

            // bottom-left
            if (mouse.X < left + radius && mouse.Y > bottom - radius)
                return (mouse.X - (left + radius)) * (mouse.X - (left + radius)) + (mouse.Y - (bottom - radius)) * (mouse.Y - (bottom - radius)) <= radius * radius;

            // bottom-right
            if (mouse.X > right - radius && mouse.Y > bottom - radius)
                return (mouse.X - (right - radius)) * (mouse.X - (right - radius)) + (mouse.Y - (bottom - radius)) * (mouse.Y - (bottom - radius)) <= radius * radius;

            // inside body (including edges)
            return true;
        }
    }
}