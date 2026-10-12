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
4. **الفيديو:** نسبة 16:9، أعلى دقة متاحة، 8 ثواني، بدون صوت. الأفضل من صورة (image-to-video).
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

Every prompt repeats the full device description on purpose, so each one works on its own and the device looks
the same in every image. Video models hallucinate less when every second is spelled out; so do image models
when every object, light and forbidden thing is.

## 1. Hero, light — `docs/images/hero-light.jpg`

```
IMAGE SPECIFICATION — "HERO, LIGHT" — READ EVERY SECTION AND FOLLOW IT LITERALLY

FORMAT
- Aspect ratio 16:9, landscape. Highest available resolution (4K if available). One single photograph.
- Photorealistic studio product photography, as if shot on a medium-format digital camera with a 100 millimetre macro lens at f/5.6. It must never look like a 3D render, CGI, an illustration or a cartoon.
- No text, letters, numbers, logos, symbols, icons, watermarks or captions anywhere in the image.

THE ONLY SUBJECT: THE DEVICE
- Exactly one device in the image: a small hardware device the size of a USB stick.
- Shape: a slim rectangular bar, 58 millimetres long, 22 millimetres wide, 9 millimetres thick. All edges and corners softly rounded with a small, even radius. The top face is perfectly flat; the bottom face rests flat on the surface.
- Body material: bead-blasted anodized aluminium in deep graphite, almost black, a very dark charcoal grey. Matte satin finish with a very fine, even micro-texture. No gloss, no mirror finish, no scratches, no fingerprints, no dust, no seams, no screws, no vents, no labels, no printed or engraved text, no logos, no stickers.
- Connector: at the left short end, one USB-C plug sticking out about 8 millimetres, the small oval USB-C shape, in dark polished steel. Not connected to anything. No cable anywhere.
- The button: exactly one button, in the exact center of the top face. A perfect circle 9 millimetres across, matte warm ivory ceramic (off-white, slightly creamy, like old piano keys), flush with the surface and very slightly domed. No symbol, letter or icon on it.
- The light ring: exactly one thin ring of light around the button, 1 millimetre wide, a perfect circle concentric with the button, separated from it by a 1 millimetre gap of graphite aluminium. It glows a soft, even, calm medium blue, like a clear sky-blue LED (not neon, not purple, not cyan). At the ring's edge there is a very faint blue glow on the aluminium within a few millimetres, and nowhere else. The device has no other lights, no screen, no other buttons.

THE SET
- The device lies on a large sheet of thick, matte, warm ivory paper (off-white, slightly warm, like fine art paper) with a very subtle, barely visible paper grain. The paper fills the entire image edge to edge. No table edge, no wall, no horizon, no other surface.
- Position: the device sits in the left third of the image, vertically centered, rotated about 20 degrees clockwise from horizontal, USB-C plug pointing toward the upper left.
- The right two thirds of the image are calm, empty ivory paper (space for a headline). Nothing else in the scene: no laptop, phone, cable, plant, cup, pen, hand, prop or reflection.

LIGHTING
- One large, soft, diffused key light from the upper left, like daylight through a big frosted window, slightly warm. One very gentle fill from the right so no shadow is black.
- One soft, natural contact shadow under the device falling toward the lower right, darkest directly under the device and fading smoothly into the paper.
- A thin, precise highlight along the device's top-left rounded edge. A soft highlight on the upper-left side of the ivory button.
- The paper stays ivory everywhere. No blue light falls on the paper.

CAMERA
- Camera about 35 degrees above the paper, looking down at the device from the front-right, three-quarter view.
- The whole device is visible and fills about 30 percent of the image width.
- The device, the button and the light ring are perfectly sharp. Only the far edge of the paper softly falls out of focus.

COLOR
- The only colors in the image: graphite (device), warm ivory (paper and button), dark steel (plug), and the single blue light ring. The ring is the only saturated color.

MOOD
- Calm, quiet, precise, expensive. The visual language of Apple, Teenage Engineering, Leica and Nothing product photography: minimal, confident, a lot of clean space.

STRICTLY FORBIDDEN
- Any text, letters, numbers, logos, symbols or icons, on the device or anywhere else.
- More than one device, a second button, extra lights, a screen, vents, screws, seams, cables.
- Hands, people, laptops, phones, keyboards, plants, cups, props.
- Neon, glowing or blue backgrounds, gradients, light beams, lens flares, sparkles, particles, smoke.
- Padlocks, shields, keys, circuit boards, code or any security clip-art.
- CGI or 3D-render look, oversaturated colors, blur on the device.
```

## 2. Hero, dark — `docs/images/hero-dark.jpg`

```
IMAGE SPECIFICATION — "HERO, DARK" — READ EVERY SECTION AND FOLLOW IT LITERALLY

FORMAT
- Aspect ratio 16:9, landscape. Highest available resolution (4K if available). One single photograph.
- Photorealistic low-key studio product photography, medium-format digital camera, 100 millimetre macro lens at f/5.6. Never a 3D render, CGI, illustration or cartoon.
- No text, letters, numbers, logos, symbols, icons, watermarks or captions anywhere.

THE ONLY SUBJECT: THE DEVICE
- Exactly one device: a slim rectangular bar 58 millimetres long, 22 millimetres wide, 9 millimetres thick, all edges and corners softly rounded with a small, even radius, flat top face.
- Body: bead-blasted anodized aluminium in deep graphite, almost black, matte satin with a very fine, even micro-texture. No gloss, scratches, fingerprints, dust, seams, screws, vents, labels, text, logos or stickers.
- Connector: one USB-C plug at the left short end, sticking out about 8 millimetres, dark polished steel, not connected to anything. No cable.
- The button: exactly one, in the exact center of the top face, a perfect circle 9 millimetres across, matte warm ivory ceramic, flush and very slightly domed, no symbol on it.
- The light ring: exactly one thin ring of light, 1 millimetre wide, a perfect circle around the button with a 1 millimetre graphite gap. It glows a soft, calm, medium blue like a clear sky-blue LED (not neon, not purple, not cyan). No other lights, no screen, no other buttons.

THE SET
- The device lies on a slab of matte, very dark graphite slate, nearly black, with a faint natural stone texture. The slate fills the entire image edge to edge. No edges, walls or horizon.
- Position: the device sits in the left third, vertically centered, rotated about 20 degrees clockwise from horizontal, USB-C plug pointing toward the upper left.
- The right two thirds are calm, empty dark slate (space for a headline). Nothing else in the scene.

LIGHTING
- Low-key, night mood. One narrow, soft strip light from the upper left draws one precise, thin highlight along the device's top-left rounded edge and a soft highlight on the edge of the ivory button. Everything else falls into deep, clean, smooth shadow, never noisy.
- The blue light ring is clearly the brightest point in the image. It casts a delicate blue glow on the aluminium within a few millimetres around it, and one faint, soft blue reflection on the slate directly next to the device. No blue anywhere else.

CAMERA
- Camera about 35 degrees above the slate, front-right three-quarter view.
- The whole device is visible, filling about 30 percent of the image width.
- The device, button and ring are perfectly sharp; the far slate softly falls out of focus.

COLOR
- Only: near-black slate, graphite device, ivory button, dark steel plug, and the single blue ring. The ring is the only saturated color.

MOOD
- Night, calm, precise, premium. Apple, Leica, Nothing product photography.

STRICTLY FORBIDDEN
- Any text, letters, numbers, logos, symbols or icons.
- More than one device, extra lights, screens, vents, screws, cables.
- Hands, people, laptops, phones, keyboards, props.
- Blue background light, neon, gradients, beams, lens flares, sparkles, smoke, haze.
- Padlocks, shields, keys, circuit boards, security clip-art.
- CGI look, noise, oversaturated colors, blur on the device.
```

## 3. The press — `docs/images/press.jpg`

```
IMAGE SPECIFICATION — "THE PRESS" — READ EVERY SECTION AND FOLLOW IT LITERALLY

FORMAT
- Aspect ratio 16:9, landscape. Highest available resolution (4K if available). One single photograph.
- Photorealistic macro photography, 100 millimetre macro lens at f/4. Never a 3D render, CGI, illustration or cartoon.
- No text, letters, numbers, logos, symbols, icons, watermarks or captions anywhere.

THE DEVICE
- Exactly one device: a slim rectangular bar 58 millimetres long, 22 millimetres wide, 9 millimetres thick, softly rounded edges and corners, flat top face.
- Body: bead-blasted anodized aluminium in deep graphite, almost black, matte satin with a very fine micro-texture. No gloss, scratches, seams, screws, vents, labels, text or logos.
- One USB-C plug at the left short end, dark polished steel, not connected to anything. No cable.
- Exactly one button in the exact center of the top face: a perfect circle 9 millimetres across, matte warm ivory ceramic, no symbol on it.
- Exactly one thin ring of light, 1 millimetre wide, around the button with a 1 millimetre graphite gap, glowing a bright but soft medium blue like a clear sky-blue LED (not neon, not purple, not cyan).

THE HAND
- Exactly one adult human right hand enters from the right side of the image. Only the index finger and part of the back of the hand are visible; the other fingers are relaxed and gently curled out of the way.
- The tip of the index finger presses the exact center of the ivory button. The fingertip skin flattens very slightly against the button, realistically. The finger covers the center of the button but the full blue ring stays visible all around it.
- Natural, healthy skin with realistic fine texture; short, clean, natural nails; no nail polish, no rings, no watch, no bracelet, no sleeve visible.
- Normal human anatomy: five fingers in total, correct joints and proportions, no extra, missing or merged fingers.
- The blue ring lights the edge of the fingertip with a faint, thin blue rim of light.

THE SET
- The device lies flat on thick, matte, warm ivory paper with a very subtle grain, rotated about 20 degrees clockwise, slightly left of center.
- The background is the same ivory paper falling into a soft, warm, out-of-focus blur. Nothing else in the scene.

LIGHTING
- One large, soft, diffused key light from the upper left, slightly warm; a gentle fill from the right.
- Soft natural shadows of the device and the finger falling toward the lower right.
- No blue light on the paper.

CAMERA
- Low camera angle, about 25 degrees above the paper, front-right three-quarter view, close-up.
- The button, the ring and the fingertip are tack sharp; everything behind falls into a smooth, shallow depth of field.

COLOR
- Only: graphite, warm ivory, natural skin tones, dark steel, and the single blue ring.

MOOD
- Calm, tactile, intimate: the exact moment of the press. Apple and Nothing product photography.

STRICTLY FORBIDDEN
- Any text, letters, numbers, logos, symbols or icons.
- More than one hand, more than one finger touching the device, extra or deformed fingers.
- More than one device, extra lights, screens, cables.
- Nail polish, jewellery, sleeves, tattoos.
- Neon, blue light on the paper, beams, lens flares, sparkles, motion blur.
- Padlocks, shields, keys, circuit boards, security clip-art.
- CGI look, oversaturated colors.
```

## 4. On the desk — `docs/images/desk.jpg`

```
IMAGE SPECIFICATION — "ON THE DESK" — READ EVERY SECTION AND FOLLOW IT LITERALLY

FORMAT
- Aspect ratio 16:9, landscape. Highest available resolution (4K if available). One single photograph.
- Photorealistic editorial photography, 50 millimetre lens at f/2.8. Never a 3D render, CGI, illustration or cartoon.
- No readable text, letters, numbers, logos, symbols, icons, watermarks or captions anywhere, including on screens and products.

THE DEVICE (the hero of the image, in sharp focus)
- Exactly one device: a slim rectangular bar 58 millimetres long, 22 millimetres wide, 9 millimetres thick, softly rounded edges and corners.
- Body: bead-blasted anodized aluminium in deep graphite, almost black, matte satin. No text, logos, seams, screws or vents.
- Its USB-C plug is fully inserted into the left side port of the laptop, so the device sticks straight out horizontally from the side of the laptop, its top face facing up.
- Exactly one round, matte, warm ivory button in the center of its top face, with exactly one thin ring of soft medium-blue light around it (like a clear sky-blue LED, not neon).

THE LAPTOP (secondary, out of focus)
- One thin, silver aluminium laptop with no visible logo, open, seen from its left side and slightly from behind, so its screen faces mostly away from the camera. The screen content is not visible.
- The laptop sits on the desk on the right side of the image, softly out of focus.

THE PHONE (secondary, out of focus)
- One plain phone with no visible logo, lying face up on the desk in front of the laptop and slightly to the right of the device, softly out of focus. Its screen shows only a calm, dark interface with no readable text, no icons and no images.

THE SET
- A minimal desk of light, natural oak with a fine, matte grain. Behind it, a softly blurred warm off-white wall. Nothing else on the desk: no cables, cups, plants, notebooks, pens, headphones or decorations.

LIGHTING
- Soft morning daylight from a large window on the left, slightly warm. Gentle, natural shadows falling to the right.
- The device's blue ring is a small, soft point of color; it does not light up the desk or the laptop.

CAMERA
- Eye level with the desk surface, slightly above it, about 15 degrees down.
- The device is in the left-center part of the image, in sharp focus. The laptop and phone fall into a smooth, shallow depth of field.

COLOR
- Only: light oak, warm off-white, silver aluminium, graphite device, ivory button, and the single small blue ring.

MOOD
- Calm, quiet, everyday premium: a real desk on a good morning. Apple and Nothing editorial photography.

STRICTLY FORBIDDEN
- Readable text, logos (including on the laptop and phone), icons, numbers, interface details.
- People, hands, more than one device, cables, mess, props.
- Neon, blue light on the desk, beams, lens flares, sparkles.
- Padlocks, shields, keys, circuit boards, security clip-art.
- CGI look, oversaturated colors.
```

## 5. Video: the press — `docs/images/press.mp4` (8 seconds)

Best as image-to-video with image 3 (or 1) as the first frame. If the hand comes out deformed, delete every
mention of the hand and the 3.0–4.0 and 4.6–5.4 beats, and replace the press beat with "the ring stops
breathing by itself, as if pressed".

```
VIDEO SPECIFICATION — "THE PRESS" — READ EVERY SECTION AND FOLLOW IT LITERALLY

FORMAT
- Aspect ratio 16:9, landscape. Highest available resolution. 8 seconds total. 24 frames per second.
- One single continuous shot. No cuts, no transitions, no fades to black, no split screens, no slow-motion ramps, no speed changes.
- No music, no voice, no sound design required. No on-screen text, captions, titles, subtitles, watermarks or logos at any moment.
- Photorealistic live-action look, as if filmed with a real cinema camera. It must never look like a 3D render, a cartoon, an illustration or an animation.

THE ONLY SUBJECT: THE DEVICE
- A small hardware device the size of a USB stick. Exactly one device is in the frame for the whole video. It never changes shape, size, color, material or proportions, never morphs, never duplicates, never splits, never floats, never moves by itself.
- Shape: a slim rectangular bar, 58 millimetres long, 22 millimetres wide, 9 millimetres thick. All edges and corners are softly rounded with a small, even radius. The top face is perfectly flat. The bottom face rests flat on the paper.
- Body material: bead-blasted anodized aluminium in a deep graphite color, almost black, very dark charcoal grey. Matte satin finish with a very fine, even micro-texture. No gloss, no mirror finish, no scratches, no fingerprints, no dust, no seams, no screws, no vents, no labels, no printed or engraved text, no logos, no icons, no stickers.
- Connector: at the left short end of the bar, a single USB-C plug sticks out about 8 millimetres. It is the small oval USB-C shape, in dark polished steel. The plug is not connected to anything. There is no cable anywhere.
- The button: exactly one button. It sits in the exact center of the top face. It is a perfect circle, 9 millimetres in diameter, made of matte warm ivory ceramic (an off-white, slightly creamy white, like old piano keys). It sits flush with the top surface, very slightly domed. There is no symbol, letter or icon printed on the button.
- The light ring: around the button there is exactly one thin ring of light, 1 millimetre wide, a perfect circle concentric with the button, with a 1 millimetre gap of graphite aluminium between the button and the ring. The light comes only from this ring. The rest of the device does not glow. The device has no other lights, no screen, no display, no other buttons.

THE SET
- The device lies on a large sheet of thick, matte, warm ivory paper (off-white, slightly warm, like fine art paper) with a very subtle, barely visible paper grain. The paper fills the entire background. There is no table edge, no wall, no horizon line, no other surface visible.
- The device lies flat on the paper, rotated about 20 degrees clockwise from horizontal, positioned slightly left of the frame's center, with the USB-C plug pointing toward the upper left.
- Nothing else is in the scene: no laptop, no phone, no cable, no keyboard, no plants, no cups, no pens, no papers, no props, no reflections of people or rooms.

LIGHTING
- One large, soft, diffused key light from the upper left, like daylight through a big frosted window, slightly warm. One very gentle fill from the right so shadows are never black.
- The device casts one soft, natural contact shadow on the paper, falling toward the lower right, darkest directly under the device and fading smoothly.
- A thin, precise highlight runs along the device's top-left rounded edge. The ivory button shows a soft, subtle highlight on its upper-left side.
- The lighting stays constant for the whole video. The light ring is the only light source that changes.

CAMERA
- Lens: 100 millimetre macro look. Camera angle: about 35 degrees above the paper, looking down at the device from the front-right, three-quarter view.
- Framing: at the start, the whole device fills about 40 percent of the frame width, with calm ivory space around it.
- Movement: one extremely slow, perfectly smooth, steady push-in toward the button over the full 8 seconds, ending with the device filling about 55 percent of the frame width. No shake, no handheld wobble, no orbit, no pan, no tilt, no zoom jumps, no refocusing pulses.
- Focus: the ivory button and the light ring are tack sharp for the entire video. The paper softly falls out of focus toward the back edge of the frame (shallow depth of field). The device itself is never blurred.

THE ACTION, SECOND BY SECOND
- 0.0 to 3.0 seconds — waiting: the device rests completely still on the paper. The light ring glows a soft, calm, medium blue (like a clear sky-blue LED, not neon, not purple, not cyan). It breathes slowly and evenly: it fades from dim to bright and back to dim, one full breath every 1.2 seconds, so you see about two and a half breaths. At its brightest, the ring casts a very faint blue glow on the aluminium within a few millimetres around it, and nowhere else. The paper stays ivory, never blue.
- 3.0 to 4.0 seconds — the finger arrives: one adult human right hand enters the frame from the right edge. Only the index finger and part of the back of the hand are visible; the hand is relaxed with the other fingers gently curled. Natural, healthy skin; short, clean, natural nails; no nail polish, no rings, no watch, no bracelet, no sleeve visible. The index fingertip moves smoothly and slowly toward the ivory button and stops directly above it.
- 4.0 to 4.6 seconds — the press: the fingertip touches the center of the ivory button and presses down gently once. The button moves down by about half a millimetre, and the fingertip skin flattens very slightly against it, realistically. The device does not move, slide, tilt or bounce on the paper. At the exact moment the button is fully pressed, the ring stops breathing.
- 4.6 to 5.4 seconds — the release: the finger lifts straight up from the button, smoothly, and withdraws back out of the frame to the right, the same way it came in. The button returns to its resting position. After 5.4 seconds no part of the hand is visible again.
- 4.6 to 6.6 seconds — confirmation: the instant the button is pressed, the light ring changes from blue to a soft, calm green (like a gentle emerald LED). The color change is a smooth half-second crossfade, never a flash. The green ring then glows steadily at medium brightness, without breathing, for about two seconds, with a very faint green glow on the aluminium right around it.
- 6.6 to 8.0 seconds — rest: the green ring slowly and smoothly fades out until the ring is completely off, leaving the graphite device and the ivory button lit only by the soft daylight. The camera's slow push-in continues and ends gently. The final frame is still, calm and clean: the device on ivory paper, light ring off.

PHYSICS AND CONTINUITY RULES
- The device stays in exactly the same position on the paper for all 8 seconds.
- Exactly one hand and exactly one index finger appear, only between 3.0 and 5.4 seconds. The hand has a normal human anatomy: five fingers in total, correct joints, correct proportions, no extra or merged fingers.
- The light ring is always a clean, unbroken circle around the button. It never becomes a different shape, never spreads into beams, never projects light rays, never sparkles, never shows lens flares.
- The only colors in the entire video are the graphite device, the ivory paper, the ivory button, natural skin tones, and the single light ring (blue, then green, then off). There is no other color anywhere.

MOOD AND REFERENCE
- Calm, quiet, precise, premium and tactile. The feel of an Apple, Nothing, Leica or Teenage Engineering product film: minimal, confident, unhurried, with a lot of clean space.

STRICTLY FORBIDDEN
- Any text, letters, numbers, logos, symbols or icons anywhere, including on the device, the button, the paper or the hand.
- Any screen, laptop, phone, monitor, keyboard, cable, or second device.
- More than one device, more than one hand, extra fingers, deformed hands, hands appearing outside 3.0 to 5.4 seconds.
- The device moving, sliding, rotating, floating, morphing, changing shape, changing color or changing size.
- Neon, glowing backgrounds, blue or green light spilling onto the paper, light beams, lens flares, sparkles, particles, smoke, haze, glitch effects, holograms, futuristic interfaces.
- Padlocks, shields, keys, circuit boards, computer code or any security clip-art.
- Cuts, camera shake, fast motion, slow motion, zoom jumps, refocusing, cartoon or 3D-render look, oversaturated colors.
```
