# Image prompts (for the owner)

This file is for the repository owner. The README and GitHub page expect a few
images. Generate them with ChatGPT using the prompts below, then save each file under
the exact name and size listed.

**هذا الملف لصاحب المشروع.** يحتاج المشروع إلى بعض الصور. انسخ كل وصف (Prompt) إلى ChatGPT،
ثم احفظ الصورة بالاسم والحجم المكتوبين تماماً داخل `docs/images/`.

## كيف تعمل / How to use

1. افتح **محادثة جديدة** في ChatGPT واستخدم نفس المحادثة للصور السبع كلها حتى يبقى شكل الجهاز موحداً.
   (Use one chat for all seven so the product looks the same everywhere.)
2. ابدأ بالصورة **(c) شكل الجهاز**، لأن باقي الصور تعتمد على تصميمه. في الصور التالية ابدأ رسالتك بالعبارة:
   `Using the same Keyra product design:` ثم الصق الوصف.
   (Generate (c) first; prefix later prompts with that sentence.)
3. **الأوصاف تبقى بالإنجليزية** لأن النتيجة أفضل. لا تترجمها.
4. إذا ظهرت حروف أو كتابة مشوّهة في الصورة، أرسل هذه الجملة في نفس المحادثة:
   `Remove all text and lettering from the image completely; keep everything else identical.`
   (The real text is added later in HTML or Figma.)
5. بعض الصور تحتاج **قصاً وتغيير حجم** بعد التوليد (مكتوب تحت كل صورة). على macOS:
   `magick input.png -gravity center -crop 1536x614+0+0 -resize 1600x640 output.png`
   (needs ImageMagick: `brew install imagemagick`). أو استخدم أي برنامج صور.
6. احفظ الملفات في `docs/images/` بالأسماء أدناه بالضبط، ثم أخبر الوكيل ليرتبها في الـ README.

| # | الملف / File | الحجم النهائي / Final size | أين يظهر / Where it is used |
|---|---|---|---|
| a | `docs/images/icon.png` | 1024 × 1024 | أيقونة التطبيق |
| b | `docs/images/hero.png` | 1600 × 640 | أعلى الـ README |
| c | `docs/images/device.png` | 1536 × 1024 | شكل الجهاز في الـ README |
| d | `docs/images/social.png` | 1280 × 640 | معاينة المشاركة على GitHub (Social preview) |
| e | `docs/images/how-it-works.png` | 1536 × 1024 | قسم "كيف يعمل" |
| f | `docs/images/phone-dark.png` | 1024 × 1536 | صورة الهاتف (الوضع الداكن) |
| g | `docs/images/social.png` | 1280 × 640 | **نفس (d)**؛ أوصاف بديلة أدناه إن لم تعجبك النتيجة |

بعد رفع الصورة (d)، فعّل المعاينة من: **Repository → Settings → General → Social preview → Upload an image**.
GitHub يوصي بـ 1280 × 640 بكسل وبحجم أقل من 1 MB.

---

## Style (already inside every prompt)

Palette: Keyra Blue `#0B57F0`, light blue `#6C9CFF`, LED blue `#2F6BFF`, ink black `#080A10`,
panel `#12151D`, paper `#F3F4F8`, white `#FFFFFF`, success green `#4ADE80` (only for the "typed" moment).
Look: calm, premium, restrained, soft studio light, matte materials. Avoid padlock or hacker clichés,
neon clutter, circuit-board textures, stock people, real brand logos, watermarks.

---

## (a) App icon: `docs/images/icon.png` (1024 × 1024)

```
Design a 1024×1024 app icon, full-bleed square (no rounded corners, no border, no mockup, no shadow outside the canvas).
Background: a smooth diagonal gradient from #4B86FF at the top-left to #0A47D8 at the bottom-right, very subtle, no noise.
Centre: a single bold white key glyph made of simple geometric strokes with perfectly round line caps — a ring-shaped bow (a circle outline) at the top,
a straight vertical shaft descending from the ring, and two short rectangular teeth pointing to the right, the lower tooth slightly shorter.
The glyph is about 52% of the canvas height, optically centred, stroke thickness about 11.5% of the glyph height, pure white #FFFFFF.
Add a very soft drop shadow under the glyph (dark blue, 35% opacity, large blur) and a faint white highlight fading from the top edge over the top 40% of the canvas (14% opacity).
Flat, crisp vector look like a modern iOS app icon. ABSOLUTELY NO TEXT, letters, numbers or extra symbols anywhere.
```

## (b) README hero banner: `docs/images/hero.png` (final 1600 × 640)

توليد: اطلب صورة عريضة 1536×1024 ثم قص الوسط: `magick in.png -gravity center -crop 1536x614+0+0 -resize 1600x640 hero.png`

```
Wide cinematic hero banner for a product called Keyra, generated in landscape. IMPORTANT composition rule: keep every important element inside the horizontal
centre band that is 60% of the image height (the top and bottom 20% will be cropped away) and leave the left 22% and right 22% of the width calm and almost empty,
because a headline will be placed there later.
Scene: a deep ink background (#080A10) fading to #12151D with one soft blue glow (#2F6BFF at 25% opacity, very large radius) behind the product.
Centre-right: a small matte graphite USB security key (a slim rounded-rectangle device about the size of a lighter, colour #1B1F2A, USB-C plug at one end,
a single round translucent button on top with a gently glowing ring of blue light #2F6BFF, a tiny debossed key glyph — a circle with a vertical stem and two teeth — next to it),
plugged into the side of a thin silver laptop (generic, no logo) that is only partly visible at the edge. Centre-left of the device, a floating smartphone
(generic black glass phone, no logo) seen from a slight angle, its screen showing an abstract dark interface: a large blue ring #6C9CFF with a white key symbol inside
and a few soft grey rounded bars as placeholder rows — NO readable text on the screen, only abstract shapes.
Thin light-blue curved line (#6C9CFF, 1.5 px) linking phone, device and laptop to suggest "wireless setup, typed into your computer".
Style: realistic soft 3D product render, shallow depth of field, very soft reflections, premium and calm. NO text, NO letters, NO logos, NO watermark anywhere in the image.
```

## (c) Product render: `docs/images/device.png` (1536 × 1024, no crop)

ابدأ بهذه الصورة. إذا لم تُكتب كلمة Keyra بشكل صحيح، اترك السطح فارغاً.

```
Studio product photograph-style 3D render of a tiny hardware password key named Keyra, on a seamless light background (#F3F4F8) with a soft long contact shadow, 3/4 front view slightly from above.
The device: a minimal slim capsule-shaped enclosure about 55 mm long, matte anodised graphite (#1B1F2A) with ultra-fine texture, softly rounded corners, one flush USB-C plug (silver) protruding from one end.
On top: ONE round pill-shaped translucent button, frosted milky-white glass, with a soft ring of electric blue light (#2F6BFF) glowing around it and a faint glow cast on the surface below.
Beside the button, a small debossed key symbol (a circle outline with a vertical stem and two small teeth to the right), and on the lower edge the single word "Keyra" debossed in a clean rounded geometric sans-serif,
spelled exactly K-e-y-r-a, small and subtle. No other text, no markings, no screws, no extra ports, no cable, no logo other than this.
Lighting: large softbox from top-left, gentle rim light, realistic materials, crisp but soft edges, 50 mm lens look, shallow depth of field.
Compose with the device filling about 60% of the width, centred, plenty of clean background around it. If the word "Keyra" cannot be spelled correctly, leave the surface blank instead of guessing.
```

## (d) Social preview: `docs/images/social.png` (final 1280 × 640)

توليد عريض 1536×1024 ثم: `magick in.png -gravity center -crop 1536x768+0+0 -resize 1280x640 social.png`
(العنوان "Keyra" يُضاف لاحقاً في المساحة الفارغة بين النصفين.)

```
Social-sharing card background in landscape, generated wide. Composition rule: all important content inside the central 2:1 band (the top and bottom 17% of the image will be cropped);
keep a 10% margin from the left and right edges.
Left half: a large rounded-square app tile (squircle) coloured with a diagonal blue gradient #4B86FF → #0A47D8, containing a bold white key glyph (a ring-shaped bow, a vertical shaft, two small teeth to the right, round line caps), with a soft shadow.
Right half: the matte graphite (#1B1F2A) slim USB key with a glowing blue (#2F6BFF) ring button on top, tilted 12 degrees, floating slightly above a very soft blue glow; behind it a very faint, large, thin blue ring (#6C9CFF at 20% opacity) as a motif.
Background: deep ink #080A10 with a subtle radial glow of #0B57F0 at 15% opacity from the top-centre. Between the two halves leave empty space — a title will be placed there later.
Style: crisp, premium, minimal, soft 3D with matte materials. NO text, NO letters, NO numbers, NO watermark.
```

## (e) How it works: `docs/images/how-it-works.png` (1536 × 1024, no crop)

```
A clean three-step explainer illustration, landscape, three equal rounded panels side by side (radius large, panel colour #12151D on a #080A10 background, 1 px hairline border rgba(255,255,255,0.09), thin gaps between panels).
Style: soft matte 3D "clay" objects, calm, minimal, consistent lighting from the top-left, palette limited to #0B57F0, #6C9CFF, #2F6BFF, #F3F4F8, #12151D and a touch of success green #4ADE80 in panel 3 only.
Panel 1 — "Plug in": a small slim graphite USB key with a pulsing blue ring button plugged into the side of a thin silver laptop (generic, no logo); on the laptop screen a blank login form with an empty field and a cursor.
Panel 2 — "Pick": a generic black phone shown upright; its screen shows an abstract dark list of rounded rows with coloured square avatars, and a big blue pill button highlighted with a fingertip about to tap it. NO readable text on the screen — only abstract bars.
Panel 3 — "Press": a close view of a finger pressing the round glowing button of the Keyra key; above it the laptop's login field now filled with a row of dots (●●●●●●●●) and a small green check badge #4ADE80.
At the top-centre of each panel place only a small circled digit: 1, 2, 3 (white digit in a #0B57F0 circle). Use NO other text anywhere — no words, no captions, no labels, no logos. If any digit is malformed, regenerate with the digits removed.
Thin light-blue connector arrows (#6C9CFF) between the panels, pointing left to right.
```

## (f) Dark phone mockup: `docs/images/phone-dark.png` (1024 × 1536, no crop)

```
Portrait product mockup: a single generic modern smartphone (black glass, thin bezels, no brand, no logo, no visible notch text) floating at a slight 8-degree angle, centred, on a deep ink background (#080A10) with a soft blue radial glow (#2F6BFF at 20% opacity) behind it and a gentle floor reflection.
The phone screen shows a dark-mode app (screen background #080A10, card surfaces #12151D): at the top a small centred rounded-square blue logo tile with a white key glyph;
in the middle a large circular progress ring (stroke 10 px, #6C9CFF, about 70% filled, clockwise) with a soft pulsing blue halo (#2F6BFF), a white-blue key glyph in its centre;
below the ring two short horizontal rounded bars in light grey (#B1B8C8 and #8D95A8) as placeholder title and subtitle, then a small pill chip (#202B41 fill) with a short bar inside, and a full-width ghost-style rounded button outline.
CRITICAL: the screen must contain NO readable text, NO letters, NO numbers — only abstract bars and shapes (real screenshots with real Arabic/English text will be composited later).
Style: photorealistic 3D render, crisp screen, subtle glass reflections, premium and calm. NO watermark.
```

## (g) GitHub social preview

الصورة **(d)** أعلاه هي معاينة المشاركة، وهي مغطاة بالفعل. إن أردت نسخة بديلة أبسط (أيقونة + جهاز على خلفية داكنة،
بمساحة فارغة واسعة للعنوان)، استخدم هذا الوصف وسمّها `docs/images/social.png` (استبدل (d)):

```
Minimal wide social card background, generated in landscape 1536×1024; keep all important content inside the central 2:1 band (top and bottom 17% will be cropped) with 10% side margins.
Dead centre, a single slim matte graphite (#1B1F2A) USB key with one round frosted-glass button ringed by electric blue light (#2F6BFF), seen straight from above, perfectly symmetrical, floating over a very soft blue glow.
Behind it, a very large thin concentric blue ring (#6C9CFF at 18% opacity) centred on the key, like a radar pulse, fading outward.
Background: deep ink #080A10 with a faint radial glow of #0B57F0 at 12% opacity from the top-centre. Leave the left and right thirds almost empty — a title and a tagline will be added later.
Style: crisp, premium, calm, soft 3D, matte materials. NO text, NO letters, NO numbers, NO logos, NO watermark.
```
