# GitHub Pages Documentation

This directory contains the GitHub Pages site for Naphome-Korvo1.

## Files

- `index.html` - Landing page with navigation
- `features.html` - Comprehensive features showcase page

## Setup Instructions

To enable GitHub Pages with GitHub Actions:

1. Go to your repository on GitHub
2. Navigate to **Settings** → **Pages**
3. Under **Source**, select **GitHub Actions**
4. The site will automatically deploy when you push to the `main` branch

## Deployment

The site is automatically deployed via `.github/workflows/pages.yml` when:
- Changes are pushed to the `main` branch in the `docs/` directory
- The workflow is manually triggered

## Access

Once deployed, the site will be available at:
`https://naptick.github.io/Naphome-Korvo1/`

## Local Development

To preview the site locally:

```bash
# Using Python
python3 -m http.server 8000 --directory docs

# Using Node.js (if you have http-server installed)
npx http-server docs -p 8000
```

Then open `http://localhost:8000` in your browser.
