# Image and video prompts (for the owner)

The logo, banner, how-it-works strip and social card are drawn in code (`docs/brand/`). Photographs and the
press video are what code cannot make. Generate them with the prompts below and hand the files to the agent.

**هذا الملف لصاحب المشروع.** الشعار والبنر ورسم «كيف يعمل» وصورة المشاركة مرسومة بالكود. الباقي صور
فوتوغرافية وفيديو، وهذي ما يسويها الكود. انسخ كل prompt كما هو (بالإنكليزية لأن النتيجة أفضل).

## طريقة الاستخدام

1. **الصور:** الأفضل Gemini (Nano Banana Pro) واختَر الدقة **4K** ونسبة **16:9**. إذا استخدمت ChatGPT اختر
   أكبر حجم أفقي (1536×1024) والوكيل يقصّها ويكبّرها.
2. **كل الصور بنفس المحادثة**، وابدأ بالصورة 1. بعدها ابدأ كل رسالة بـ:
   `Same Keyra device, same materials and lighting as before:` ثم الصق الـ prompt.
3. إذا طلع أي نص أو شعار مشوّه بالصورة أرسل:
   `Remove every letter, logo and marking from the device and the scene; keep everything else identical.`
4. **الفيديو:** Veo بـ Gemini، نسبة 16:9، أعلى دقة متاحة (1080p أو 4K)، 8 ثواني، بدون صوت.
5. أرسل الملفات للوكيل كما هي. هو يسمّيها ويضغطها ويحطها بالصفحة مع عبارة
   «Concept render» لأن الجهاز الحقيقي اليوم لوحة ESP32-S3 مكشوفة، وهذا التصميم تصوّر لعلبة مستقبلية.

| # | الملف | الحجم | مكانه |
|---|---|---|---|
| 1 | `docs/images/hero-light.jpg` | 16:9، 4K | أعلى الصفحة، الوضع الفاتح |
| 2 | `docs/images/hero-dark.jpg` | 16:9، 4K | أعلى الصفحة، الوضع الداكن |
| 3 | `docs/images/press.jpg` | 16:9، 4K | قسم «كيف يعمل» |
| 4 | `docs/images/desk.jpg` | 16:9، 4K | قسم «لماذا Keyra» |
| 5 | `docs/images/press.mp4` | 16:9، 8 ث | فيديو الضغطة تحت «كيف يعمل» |

Brand reference for every prompt (already written into each one): graphite `#16171A`, ivory `#F2F2EF`,
warm paper `#F4F3EF`, and exactly one colour: the LED's blue `#2F6BFF`. Calm, precise, premium, real.

---

## 1. Hero, light — `docs/images/hero-light.jpg`

```
Ultra-realistic studio product photograph, 16:9 landscape, highest resolution and detail, shot on a medium-format camera with a 100 mm macro lens at f/5.6, perfectly sharp focus on the device.

Subject: "Keyra", a small hardware password vault the size of a USB stick: a slim rectangular bar about 58 mm long, 22 mm wide and 9 mm thick, with softly radiused edges and corners. Body machined from bead-blasted anodized aluminium in deep graphite (#16171A), fine satin texture, crisp chamfer catching a thin line of light. A USB-C plug in polished graphite steel at one short end. On the top face, one flush circular push button in matte warm ivory ceramic (#F2F2EF), 9 mm across, set in the middle of a hairline-thin light ring that glows a soft, even LED blue (#2F6BFF), with a faint blue bloom on the aluminium around it. That ring is the only colour in the entire image. No text, no logo, no screws, no stickers, no visible electronics.

Scene: the device lies at a slight angle (about 20°) on a sheet of warm ivory paper (#F4F3EF) with a very subtle paper grain, placed on the left third of the frame. Large calm negative space on the right two thirds for a headline. One large soft key light from the upper left, a gentle fill from the right, a long, soft, natural contact shadow. Minimal, quiet, expensive, in the visual language of Apple, Teenage Engineering, Leica and Nothing product photography.

Avoid: text, lettering, logos, reflections of people, extra objects, plants, coffee cups, hands, neon, glowing blue backgrounds, gradients, circuit boards, padlocks, shields, keys, cartoon or 3D-render look, oversaturated colours, depth-of-field blur on the device itself.
```

## 2. Hero, dark — `docs/images/hero-dark.jpg`

```
Same Keyra device, same materials and lighting as before: ultra-realistic studio product photograph, 16:9 landscape, highest resolution and detail, 100 mm macro lens at f/5.6.

The graphite Keyra stick lies at a slight angle on a slab of matte dark graphite slate (#0B0C0E) with a faint natural stone texture, on the left third of the frame. Low-key lighting: one narrow soft strip light from the upper left drawing a precise highlight along the device's top chamfer and the edge of the ivory button; everything else falls into deep, clean shadow. The thin LED ring around the ivory button glows soft blue (#2F6BFF) and casts a delicate blue glow on the aluminium and a faint reflection on the slate. That ring is the only colour in the image. Large dark negative space on the right two thirds for a headline.

Mood: night, calm, precise, premium. Avoid: text, logos, extra objects, smoke, neon, blue background light, lens flares, sparkles, circuit patterns, padlocks, CGI look.
```

## 3. The press, macro — `docs/images/press.jpg`

```
Same Keyra device, same materials and lighting as before: ultra-realistic macro photograph, 16:9 landscape, highest resolution, 100 mm macro lens at f/4, focus exactly on the button.

A human index finger (natural skin, short clean nail, no jewellery) presses the ivory ceramic button on the top of the graphite Keyra stick, seen from a low three-quarter angle. The thin LED ring around the button glows bright soft blue (#2F6BFF), lighting the edge of the fingertip with a faint blue rim. The device rests on warm ivory paper (#F4F3EF); the background falls off to a soft, warm, out-of-focus ivory. One soft key light from the upper left. Calm, tactile, intimate: the moment of the press.

Avoid: text, logos, other fingers in odd positions, extra hands, rings, nail polish, motion blur, neon, sparkles, CGI look.
```

## 4. On the desk — `docs/images/desk.jpg`

```
Same Keyra device, same materials and lighting as before: ultra-realistic editorial photograph, 16:9 landscape, highest resolution, 50 mm lens at f/2.8.

A minimal, light oak desk in soft morning daylight from a window on the left. A closed-bezel silver laptop seen from the side and slightly behind, its screen turned mostly away from the camera, softly out of focus. The graphite Keyra stick is plugged into the laptop's USB-C port, in sharp focus, its thin LED ring glowing soft blue (#2F6BFF). Next to it on the desk, slightly out of focus, a phone lying face up whose screen shows only a calm dark interface with no readable text. Warm ivory and graphite tones, lots of air, nothing else on the desk.

Avoid: readable text anywhere, logos (including on the laptop and phone), people, mugs, plants, cables in a mess, neon, CGI look.
```

## 5. Video: the press — `docs/images/press.mp4` (Veo, 8 seconds)

```
Ultra-realistic 8-second product film, 16:9, highest available resolution, 24 fps, no music, no voice, no text on screen, one continuous shot, no cuts.

Scene: a small graphite aluminium USB-C stick ("Keyra", 58 mm long, softly radiused edges, one flush matte ivory circular button in the middle of its top face, surrounded by a hairline-thin LED ring) is plugged into the side of a silver laptop on a warm ivory desk. Soft daylight from the left. In the soft-focus background, the laptop screen shows a simple sign-in box with an empty password field and a blinking cursor; no readable words.

0–2 s: the camera slowly pushes in toward the stick. The LED ring breathes soft blue (#2F6BFF), fading up and down once every 1.2 seconds, calm and even.
2–4 s: a human index finger enters from the right and presses the ivory button once, firmly, then lifts away.
4–6 s: the instant it is pressed, in the background the password field fills from left to right with round dots, one by one, quickly and smoothly, as if typed by a keyboard. The LED ring stops breathing and glows a steady soft green for one second.
6–8 s: the LED ring fades to off. The camera settles, still and quiet, on the stick in sharp focus.

Style: premium, calm, tactile; Apple and Nothing product films; real physics, real reflections on the aluminium, gentle shallow depth of field. Avoid: readable text, logos, extra hands, sparkles, neon, glitch effects, fast cuts, camera shake, CGI look.
```
