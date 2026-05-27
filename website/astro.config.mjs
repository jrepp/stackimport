// @ts-check
import mdx from "@astrojs/mdx";
import sitemap from "@astrojs/sitemap";
import starlight from "@astrojs/starlight";
import tailwindcss from "@tailwindcss/vite";
import { defineConfig, fontProviders } from "astro/config";

export default defineConfig({
  site: "https://jrepp.github.io",
  base: "/stackimport",
  fonts: [
    {
      name: "Inter",
      cssVariable: "--font-inter",
      provider: fontProviders.fontsource(),
      weights: [400, 500, 600, 700],
      subsets: ["latin"],
    },
  ],
  integrations: [
    sitemap(),
    starlight({
      title: "StackImport",
      description: "HyperCard stack import and format documentation",
      customCss: ["./src/styles/global.css"],
      editLink: {
        baseUrl: "https://github.com/jrepp/stackimport/tree/master",
      },
      lastUpdated: true,
      social: [
        {
          icon: "github",
          label: "GitHub",
          href: "https://github.com/jrepp/stackimport",
        },
      ],
      sidebar: [
        {
          label: "Documentation",
          items: [
            { label: "Overview", link: "/" },
            {
              label: "Install And Integrate",
              items: [
                {
                  label: "Framework Integration",
                  link: "/install/framework/",
                },
              ],
            },
            {
              label: "CLI",
              items: [
                { label: "Overview", link: "/cli/" },
                { label: "Import Mode", link: "/cli/import/" },
                { label: "Scan Mode", link: "/cli/scan/" },
                { label: "ROM Mode", link: "/cli/rom/" },
                { label: "Corpus Mode", link: "/cli/corpus/" },
              ],
            },
            {
              label: "C API",
              items: [
                { label: "Design", link: "/c-api/" },
                { label: "Context Lifecycle", link: "/c-api/context/" },
                { label: "Import Operation", link: "/c-api/import/" },
                { label: "Platform And Logs", link: "/c-api/platform/" },
                { label: "Resource Payloads", link: "/c-api/resources/" },
                { label: "Sound Conversion", link: "/c-api/sound/" },
                { label: "Reference", link: "/c-api/reference/" },
              ],
            },
            {
              label: "Format Understanding",
              items: [
                { label: "Overview", link: "/format/" },
                { label: "Block Types", link: "/format/blocks/" },
                {
                  label: "Resource Conversions",
                  link: "/resources/conversions/",
                },
              ],
            },
            {
              label: "Planning",
              items: [
                { label: "Overview", link: "/plans/" },
                {
                  label: "Code Quality Remediation",
                  link: "/plans/code-quality-remediation/",
                },
                {
                  label: "Disassembly Annotation",
                  link: "/plans/disassembly-annotation/",
                },
              ],
            },
            {
              label: "Workflows",
              items: [
                { label: "Overview", link: "/workflows/" },
                {
                  label: "C++ Parser Or Library Change",
                  link: "/workflows/cpp-change/",
                },
                {
                  label: "Documentation Site Change",
                  link: "/workflows/docs-site-change/",
                },
                {
                  label: "Format Reverse Engineering",
                  link: "/workflows/format-reverse-engineering/",
                },
                {
                  label: "Corpus Import Run",
                  link: "/workflows/corpus-import-run/",
                },
                {
                  label: "Release And Version Update",
                  link: "/workflows/release-version-update/",
                },
              ],
            },
          ],
        },
      ],
      tableOfContents: false,
    }),
    mdx(),
  ],
  vite: {
    plugins: [tailwindcss()],
  },
});
