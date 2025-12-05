# GitHub Pages Documentation

This directory contains documentation for GitHub Pages.

## Files

- `index.md` - Main landing page
- `wake-word-training-results.md` - Complete training and deployment results
- `_config.yml` - Jekyll configuration for GitHub Pages

## Publishing to GitHub Pages

### Option 1: Automatic (Recommended)

1. Push to GitHub
2. Go to: **Settings** → **Pages**
3. Select source: **Deploy from a branch**
4. Choose branch: `main` (or `gh-pages`)
5. Select folder: `/docs`
6. Click **Save**

GitHub will automatically build and publish the site.

### Option 2: Manual

If you prefer to use a `gh-pages` branch:

```bash
git checkout -b gh-pages
git subtree push --prefix docs origin gh-pages
```

## Viewing Locally

To preview the site locally with Jekyll:

```bash
cd docs
bundle install
bundle exec jekyll serve
```

Then visit: http://localhost:4000

## Content

The documentation includes:
- Training results and metrics
- Deployment status
- Conversion pipeline details
- Training improvement recommendations
- Technical implementation notes

---

**Note:** GitHub Pages uses Jekyll by default. The `_config.yml` file configures the site theme and navigation.
