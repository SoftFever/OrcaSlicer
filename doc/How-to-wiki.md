# How to Contribute to the Wiki

This guide explains how to contribute to the Orca Slicer wiki.

Orca Slicer uses GitHub's wiki feature, which allows users and developers to create and edit documentation collaboratively.

We encourage all developers and users to contribute to the wiki by updating existing pages and adding new content. This helps keep the documentation up-to-date and useful for everyone.

When developing new features, please consider updating the wiki to reflect these changes. This ensures that users have access to the latest information and can make the most of the features.

- [Wiki Structure](#wiki-structure)
  - [Home](#home)
    - [Index and Navigation](#index-and-navigation)
  - [File Naming and Organization](#file-naming-and-organization)
- [Formatting and Style](#formatting-and-style)
  - [Markdown Formatting](#markdown-formatting)
  - [Alerts and Callouts](#alerts-and-callouts)
- [Images](#images)
  - [Image Naming](#image-naming)
  - [Image Placement](#image-placement)
  - [Linking Images](#linking-images)
    - [Examples](#examples)
    - [Avoid the Following](#avoid-the-following)
    - [Resize Images](#resize-images)
  - [Image Cropping and Highlighting](#image-cropping-and-highlighting)
  - [Recommended Formats](#recommended-formats)
- [Structuring Content](#structuring-content)
- [Commands and Code Blocks](#commands-and-code-blocks)
- [External Links](#external-links)

## Wiki Structure

Each wiki page is a Markdown file located in the `doc` directory of the repository. The wiki is organized into various sections, each covering different areas of the project.

### Home

The starting point of the Orca Slicer wiki is the **Home** page. From there, you can navigate to different sections and topics related to the project.

When creating a new page or section, be sure to link it from the Home page under the appropriate category.

- **Print Settings:** Detailed explanations of print settings, tips, and tricks for optimizing print quality.
- **Printer Calibration:** Step-by-step calibration tests in Orca Slicer, including how to interpret the results.
- **Developer Section:** Information for developers and contributors on building Orca Slicer, localization, and developer resources.

#### Index and Navigation

Github Wiki uses the name of the files as identifiers for the pages. To link to a page, use the file name without the `.md` extension.
If the file is inside a subdirectory, dont include the subdirectory in the link. Instead, link directly to the file from the Home page.

For example, if you create a new page `doc/calibration/flow-rate-calib.md`, link it as follows:

```markdown
[Calibration Guide](Calibration)
```

For pages with extensive content, it's helpful to include a table of contents at the beginning. This allows users to quickly find and access different sections of the page.

```markdown
- [Wiki Structure](#wiki-structure)
  - [Home](#home)
    - [Index and Navigation](#index-and-navigation)
  - [File Naming and Organization](#file-naming-and-organization)
- [Formatting and Style](#formatting-and-style)
```

> [!NOTE]
> If you're adding a new section, follow the existing structure and ensure it doesn't already fit within an existing category. Link it from the Home page accordingly.

### File Naming and Organization

When creating new pages, follow these file naming conventions:

- Use unique file names to avoid conflicts.
- Use descriptive names that reflect the page's content.
- Use kebab-case for filenames (e.g., `How-to-wiki.md`).
- If the page belongs to a specific section, include the section name in the file name. For example, calibration pages should end with `-calib.md` (e.g., `flow-rate-calib.md`, `pressure-advance-calib.md`).
- Place files in the appropriate subdirectory when applicable (e.g., `doc/calibration/` for calibration-related content).

## Formatting and Style

Please adhere to the following style and formatting conventions when contributing to the wiki.

### Markdown Formatting

The wiki uses standard Markdown syntax for formatting and aims to maintain a consistent style across all pages. Avoid using raw HTML tags and prefer Markdown formatting instead.

Ensure your indentation is consistent, especially for code blocks and lists.

Refer to the [GitHub Markdown Guide](https://guides.github.com/features/mastering-markdown/) for more information on Markdown syntax.

### Alerts and Callouts

To add alerts or notes, use GitHub’s Markdown alert syntax:

```markdown
> [!NOTE]
> Useful information that users should know, even when skimming content.

> [!TIP]
> Helpful advice for doing things better or more easily.

> [!IMPORTANT]
> Key information users need to know to achieve their goal.

> [!WARNING]
> Urgent info that needs immediate user attention to avoid problems.

> [!CAUTION]
> Advises about risks or negative outcomes of certain actions.
```

> [!NOTE]
> Useful information that users should know, even when skimming content.

> [!TIP]
> Helpful advice for doing things better or more easily.

> [!IMPORTANT]
> Key information users need to know to achieve their goal.

> [!WARNING]
> Urgent info that needs immediate user attention to avoid problems.

> [!CAUTION]
> Advises about risks or negative outcomes of certain actions.

Refer to the [GitHub Alert Guide](https://docs.github.com/get-started/writing-on-github/getting-started-with-writing-and-formatting-on-github/basic-writing-and-formatting-syntax#alerts) for more details.

## Images

Images are encouraged to enhance the clarity and quality of the wiki content. They help illustrate concepts, provide examples, and improve readability.

> [!CAUTION]
> Do not use images from third-party sources unless you have the proper permissions.

### Image Naming

- Use clear, descriptive filenames that reflect the image content.
- For section-specific images, include the section name or initials. For example, images related to Pressure Advance could be named `pa-[description].png`.

### Image Placement

- General images should be placed in the `doc/images/` directory.
- Section-specific images should be stored in their corresponding subdirectories (e.g., `doc/images/calibration/` for calibration content).

### Linking Images

Always use raw GitHub URLs for image links to ensure correct display:

Format = `![[filename]](` + Base URL + filename.extension + Raw tag + `)`

- Base URL:
  ```markdown
  https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/
  ```
- Raw tag:
  ```markdown
  ?raw=true
  ```

#### Examples

- For an image in `doc/images/` named `example.png`:

  ```markdown
  ![example](https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/example.png?raw=true)
  ```

- For an image in a subdirectory like `doc/images/calibration/pa-example.svg`:
  ```markdown
  ![pa-example](https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/calibration/pa-example.svg?raw=true)
  ```

> [!IMPORTANT]
> New or Moved Images will not appear in the preview until the Pull Request is merged. Double-check your paths.  
> If you are changing an image path, ensure all links to that image are updated accordingly.

#### Avoid the Following

- Relative paths
- GitHub Assets, user content, or user-images URLs
- External image links from unreliable or temporary platforms
- Images containing personal or sensitive information
- Using images for content that can be expressed in text, such as equations or code—use Markdown syntax or Mermaid/Math formatting instead.

> [!NOTE]
> When contributing section-specific images, follow the naming conventions and directory structure.

#### Resize Images

Avoid the resize of images and let the Wiki handle it automatically.

If resizing is necessary (e.g., for thumbnails), use the following syntax:

HTML Format = `<img src="` + Base URL + filename.extension + Raw tag + `" alt="` + filename + `"` + size limit.
Example:
```html
   <img src="https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/InputShaping/IS_damp_marlin_print_measure.jpg?raw=true" alt="Input_Shaping" height="200">
```

### Image Cropping and Highlighting

To ensure clarity:

- Crop images to focus on relevant areas.
- Use annotations like arrows or shapes (circles, rectangles) to highlight key parts—but avoid overloading the image.

### Recommended Formats

- **JPG:** Suitable for photographs. Avoid for images with text or fine detail due to compression artifacts.
- **PNG:** Ideal for screenshots or images with transparency. Ensure sufficient contrast for light and dark modes.
- **SVG:** Preferred when possible. SVGs support theme adaptation (light/dark mode), making them ideal for icons and diagrams.

## Structuring Content

Each wiki page should have a clear objective, which helps determine the structure of the content. After a brief introduction, use one of the following formats:

- **Step-by-Step Guide:** Organize content into sections and subsections for tasks requiring sequential actions (e.g., calibration procedures).
- **GUI-Based Reference:** If sequence isn’t crucial, structure the content following Orca Slicer’s GUI. This format works well for configurable settings or feature overviews.
  - Example: Explain **Layer Height** before **Initial Layer Height**, as the former applies globally while the latter is specific to the first layer.

## Commands and Code Blocks

When adding commands or code blocks please use the [Code Block with Syntax Highlighting feature of Markdown](https://docs.github.com/en/get-started/writing-on-github/working-with-advanced-formatting/creating-and-highlighting-code-blocks#syntax-highlighting).

- Use triple backticks (```) to enclose code blocks.
- Specify the language for proper highlighting and readability.

````markdown
```json
{
  "key": "value"
}
```
````

```json
{
  "key": "value"
}
```

## External Links

Be careful when linking to external resources. Ensure that the links are relevant and reliable.
Papers, articles, and other resources should be cited properly.
