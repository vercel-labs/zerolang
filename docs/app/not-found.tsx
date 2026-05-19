import { ButtonLink } from "@/components/button";

export default function NotFound() {
  return (
    <main className="mx-auto w-[min(100%-3rem,42rem)] py-16">
      <p className="mb-2 text-[0.8125rem] font-medium uppercase tracking-[0.04em] text-muted">404</p>
      <h1 className="mb-4 text-3xl font-bold tracking-tight">Not found</h1>
      <p className="mb-6 text-muted">The page you requested does not exist.</p>
      <ButtonLink href="/">Go home</ButtonLink>
    </main>
  );
}
