# GitHub Pages Setup Guide

## Quick Setup

### Step 1: Enable GitHub Pages

1. Go to your GitHub repository
2. Click **Settings** → **Pages** (left sidebar)
3. Under **Source**, select:
   - **Deploy from a branch**
   - **Branch:** `main` (or your default branch)
   - **Folder:** `/docs`
4. Click **Save**

### Step 2: Verify

GitHub will build and publish your site automatically. After a few minutes:

- Visit: `https://YOUR_USERNAME.github.io/Naphome-Korvo1/`
- Or: `https://YOUR_ORG.github.io/Naphome-Korvo1/`

## Files Created

✅ **Documentation Files:**
- `docs/index.md` - Landing page
- `docs/wake-word-training-results.md` - Complete results
- `docs/_config.yml` - Jekyll configuration
- `docs/README.md` - Setup instructions

## Content Overview

The published page includes:

1. **Executive Summary** - High-level results
2. **Model Status** - Conversion success and test results
3. **Deployment** - ESP32 integration status
4. **Conversion Pipeline** - Technical details
5. **Training Recommendations** - Improvement strategies
6. **Technical Details** - Architecture and challenges
7. **Next Steps** - Action items

## Customization

### Update Theme

Edit `docs/_config.yml`:
```yaml
theme: jekyll-theme-minimal  # Change to your preferred theme
```

### Add More Pages

Create new `.md` files in `docs/` with front matter:
```markdown
---
title: Your Page Title
layout: default
---

Your content here...
```

## Troubleshooting

**Site not building?**
- Check GitHub Actions tab for build errors
- Verify `_config.yml` syntax is correct
- Ensure markdown files have proper front matter

**Changes not showing?**
- Wait 1-2 minutes for GitHub to rebuild
- Hard refresh browser (Ctrl+F5 / Cmd+Shift+R)
- Check GitHub Pages build logs

## Preview Locally

```bash
cd docs
bundle install
bundle exec jekyll serve
```

Visit: http://localhost:4000

---

**Your site will be live at:**
`https://YOUR_USERNAME.github.io/Naphome-Korvo1/`
